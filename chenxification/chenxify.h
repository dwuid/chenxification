
#pragma once

#include "llvm/Pass.h"

#include "llvm/IR/Type.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"

#include <set>
#include <map>
#include <random>
#include <vector>
#include <string>
#include <cstdint>

class Chenxify : public llvm::FunctionPass {
public:
    static char ID;

public:
    Chenxify() : FunctionPass(ID), engine_(device_()) {
    }

    virtual ~Chenxify() {
    }

    virtual bool runOnFunction(llvm::Function &function) override;
    virtual void getAnalysisUsage(llvm::AnalysisUsage &usage) const override;

private:
    size_t get_average_block_length(llvm::Function &function) const;
    void split_basic_blocks(llvm::Function &function, size_t split_index);

    void assign_indices(llvm::Function &function, llvm::BasicBlock &entry);
    void add_control_blocks(llvm::Function &function, llvm::BasicBlock &entry);
    void connect_control_blocks(llvm::Function &function,
                                llvm::BasicBlock &entry);

    void rewrite_branches(llvm::Function &function);
    void replace_uses(llvm::Function &function,
                      llvm::DominatorTreeWrapperPass &dominator_pass);

private:
    const size_t minimum_split_size = 3;

    static const std::string prefix_;
    std::random_device device_;
    std::default_random_engine engine_;

    std::vector<llvm::BasicBlock*> branching_blocks_;
    std::set<llvm::BasicBlock*> branch_targets_;

    std::map<llvm::BasicBlock*, uint32_t> block_indices_;

    llvm::BasicBlock *block_entry_ = nullptr,
        *block_dispatch_ = nullptr,
        *block_latch_ = nullptr;

    llvm::Instruction *vip_ = nullptr;
    llvm::SwitchInst *switch_ = nullptr;

    llvm::PHINode *phi_dispatch_ = nullptr,
        *phi_latch_ = nullptr;
};
