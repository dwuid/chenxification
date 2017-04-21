
#include "chenxify.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <random>

using namespace std;
using namespace llvm;

bool Chenxify::runOnFunction(Function &function) {
    if(function.size() <= 1) {
        return false;
    }

    errs() << "Chenxifying ";
    errs().write_escaped(function.getName()) << ".\n";

    auto split_index = get_average_block_length(function);
    if(split_index < minimum_split_size) {
        split_index = 0;
    }

    // FIXME: This does not work yet. ¯\_(ツ)_/¯
    //split_basic_blocks(function, split_index);

    BasicBlock &entry = function.front();
    auto &dominator_pass = getAnalysis<DominatorTreeWrapperPass>();

    assign_indices(function, entry);
    add_control_blocks(function, entry);
    connect_control_blocks(function, entry);

    rewrite_branches(function);
    replace_uses(function, dominator_pass);

    return true;
}

void Chenxify::getAnalysisUsage(AnalysisUsage &usage) const {
    usage.addRequired<DominatorTreeWrapperPass>();
}

size_t Chenxify::get_average_block_length(Function &function) const {
    if(!function.size()) {
        return 0;
    }

    size_t instruction_count = 0;
    for(BasicBlock &block : function) {
        for(auto i = block.getFirstInsertionPt(), e = block.end();
                i != e; ++i) {
            instruction_count += 1;
        }
    }

    return instruction_count / function.size();
}

void Chenxify::split_basic_blocks(Function &function, size_t split_index) {
    if(!split_index) {
        return;
    }

    vector<BasicBlock*> blocks;
    for(BasicBlock &block : function) {
        blocks.push_back(&block);
    }

    for(BasicBlock *block : blocks) {
        if(block->size() < 2 * minimum_split_size) {
            continue;
        }

        auto j = 0u;
        for(auto i = block->getFirstInsertionPt(), e = block->end();
                i != e; ++i, ++j) {
            if(j == split_index) {
                i->dump();
                block->splitBasicBlock(&*i);
            }
        }
    }
}

void Chenxify::assign_indices(Function &function, BasicBlock &entry) {
    branching_blocks_.clear();
    branch_targets_.clear();

    for(BasicBlock &block : function) {
        auto terminator = block.getTerminator();
        if(BranchInst *branch = dyn_cast<BranchInst>(terminator)) {
            branching_blocks_.push_back(&block);

            if(branch->isUnconditional()) {
                branch_targets_.insert(branch->getSuccessor(0));
            } else {
                branch_targets_.insert(branch->getSuccessor(0));
                branch_targets_.insert(branch->getSuccessor(1));
            }
        }
    }

    block_indices_.clear();
    block_indices_[&entry] = 0;

    set<uint32_t> random;
    uniform_int_distribution<uint32_t> distribution;

    while(random.size() != branch_targets_.size()) {
        random.insert(distribution(engine_));
    }

    auto block = branch_targets_.begin();
    for(auto i = random.cbegin(), e = random.end(); i != e; ++i, ++block) {
        block_indices_[*block] = *i;
    }
}

void Chenxify::add_control_blocks(Function &function, BasicBlock &e) {
    auto &context = function.getContext();
    auto int_type = IntegerType::getInt32Ty(context);

    const auto entry = prefix_ + "entry";
    const auto dispatch = prefix_ + "dispatch";
    const auto latch = prefix_ + "latch";

    block_entry_ = BasicBlock::Create(context, entry, &function, &e);
    block_dispatch_ = BasicBlock::Create(context, dispatch, &function, &e);
    block_latch_ = BasicBlock::Create(context, latch, &function, &e);

    const auto phi_dispatch = prefix_ + "phi_dispatch";
    const auto phi_latch = prefix_ + "phi_latch";

    phi_dispatch_ = PHINode::Create(int_type, 0, phi_dispatch, block_dispatch_);
    phi_latch_ = PHINode::Create(int_type, 0, phi_latch, block_latch_);
}

void Chenxify::connect_control_blocks(Function &function, BasicBlock &entry) {
    auto &context = function.getContext();
    auto int_type = IntegerType::getInt32Ty(context);

    vip_ = new AllocaInst(int_type, prefix_ + "alloca_vip", block_entry_);
    new StoreInst(Constant::getNullValue(int_type), vip_, block_entry_);

    auto load_vip = new LoadInst(vip_, prefix_ + "vip", block_entry_);

    phi_dispatch_->addIncoming(load_vip, block_entry_);
    phi_dispatch_->addIncoming(phi_latch_, block_latch_);

    BranchInst::Create(block_dispatch_, block_entry_);
    BranchInst::Create(block_dispatch_, block_latch_);

    switch_ = SwitchInst::Create(phi_dispatch_, &entry, 0, block_dispatch_);

    for(BasicBlock *block : branch_targets_) {
        auto index = block_indices_[block];
        switch_->addCase(ConstantInt::get(int_type, index), block);
    }
}

void Chenxify::rewrite_branches(Function &function) {
    auto &context = function.getContext();
    auto int_type = IntegerType::getInt32Ty(context);

    for(BasicBlock *block : branching_blocks_) {
        const string label = prefix_ + "forward";

        auto terminator = block->getTerminator();
        BranchInst *branch = dyn_cast<BranchInst>(terminator);

        if(branch->isUnconditional()) {
            auto successor = branch->getSuccessor(0);
            successor->removePredecessor(block);

            uint32_t delta = block_indices_[successor] - block_indices_[block];
            auto delta_constant = ConstantInt::get(int_type, delta);

            auto *forward = BinaryOperator::Create(Instruction::Add,
                                                   phi_dispatch_,
                                                   delta_constant,
                                                   label, branch);

            branch->setSuccessor(0, block_latch_);
            phi_latch_->addIncoming(forward, block);
            continue;
        }

        auto successor_t = branch->getSuccessor(0);
        auto successor_f = branch->getSuccessor(1);

        successor_t->removePredecessor(block);
        successor_f->removePredecessor(block);

        auto block_t = BasicBlock::Create(context, label, &function, block);
        auto block_f = BasicBlock::Create(context, label, &function, block);

        uint32_t delta = block_indices_[successor_t] - block_indices_[block];
        auto delta_true = ConstantInt::get(int_type, delta);

        auto *forward_t = BinaryOperator::Create(Instruction::Add,
                                                 phi_dispatch_,
                                                 delta_true,
                                                 label, block_t);

        delta = block_indices_[successor_f] - block_indices_[block];
        auto delta_false = ConstantInt::get(int_type, delta);

        auto *forward_f = BinaryOperator::Create(Instruction::Add,
                                                 phi_dispatch_,
                                                 delta_false,
                                                 label, block_f);

        BranchInst::Create(block_latch_, block_t);
        BranchInst::Create(block_latch_, block_f);

        phi_latch_->addIncoming(forward_t, block_t);
        phi_latch_->addIncoming(forward_f, block_f);

        branch->setSuccessor(0, block_t);
        branch->setSuccessor(1, block_f);
    }
}

void Chenxify::replace_uses(Function &function,
                            DominatorTreeWrapperPass &dominators) {
    dominators.runOnFunction(function);
    DominatorTree &dominator_tree = dominators.getDomTree();

    std::set<Instruction*> conflicts;
    for(BasicBlock &block : function) {
        for(Instruction &instruction : block.getInstList()) {
            for(auto i = 0u, e = instruction.getNumOperands(); i != e; ++i) {
                Instruction *operand =
                    dyn_cast<Instruction>(instruction.getOperand(i));
                if(!operand) {
                    continue;
                }

                const Use &use = instruction.getOperandUse(i);
                if(!dominator_tree.dominates(operand, use)) {
                    conflicts.insert(operand);
                }
            }
        }
    }

    for(Instruction *operand : conflicts) {
        if(isa<AllocaInst>(operand)) {
            operand->removeFromParent();
            operand->insertAfter(block_entry_->getFirstNonPHI());
            continue;
        }

        auto type = operand->getType();
        auto initialization = new AllocaInst(type, vip_);
        new StoreInst(Constant::getNullValue(type), initialization, vip_);

        auto default_ = new LoadInst(initialization, prefix_ + "default", vip_);
        auto phi = PHINode::Create(type, 0, prefix_ + "phi", phi_dispatch_);

        phi->addIncoming(default_, block_entry_);
        operand->replaceAllUsesWith(phi);

        auto phi_latch = PHINode::Create(type, 0, prefix_ + "phi_latch",
                                         phi_latch_);
        phi_latch->addIncoming(operand, operand->getParent());

        for(auto i = pred_begin(block_latch_), e = pred_end(block_latch_);
                i != e; ++i) {
            if(*i != operand->getParent()) {
                phi_latch->addIncoming(phi, *i);
            }
        }

        phi->addIncoming(phi_latch , block_latch_);
    }
}

char Chenxify::ID = 0;
const string Chenxify::prefix_ = "__chenxify_";

static RegisterPass<Chenxify> _("chenxify",
                                "Pass to flatten the control flow graph"
                                " (Chenxification)", false, false);
