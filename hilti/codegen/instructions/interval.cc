
#include <hilti-intern.h>

#include "../stmt-builder.h"

using namespace hilti;
using namespace codegen;

void StatementBuilder::visit(statement::instruction::interval::Equal* i)
{
    auto op1 = cg()->llvmValue(i->op1());
    auto op2 = cg()->llvmValue(i->op2());

    auto v1 = cg()->llvmExtractValue(op1, 0);
    auto v2 = cg()->llvmExtractValue(op2, 0);
    auto cmp1 = cg()->builder()->CreateICmpEQ(v1, v2);

    v1 = cg()->llvmExtractValue(op1, 1);
    v2 = cg()->llvmExtractValue(op2, 1);
    auto cmp2 = cg()->builder()->CreateICmpEQ(v1, v2);

    auto result = cg()->builder()->CreateAnd(cmp1, cmp2);
    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::Add* i)
{
    auto op1 = cg()->llvmValue(i->op1());
    auto op2 = cg()->llvmValue(i->op2());

    auto result = builder()->CreateAdd(op1, op2);

    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::Mul* i)
{
    auto op1 = cg()->llvmValue(i->op1());
    auto op2 = cg()->llvmValue(i->op2());

    auto result = builder()->CreateMul(op1, op2);

    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::AsDouble* i)
{
    auto op1 = cg()->llvmValue(i->op1());

    auto val = cg()->builder()->CreateUIToFP(op1, cg()->llvmTypeDouble());
    auto result = cg()->builder()->CreateFDiv(val, cg()->llvmConstDouble(1e9));

    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::AsInt* i)
{
    auto op1 = cg()->llvmValue(i->op1());

    auto result = cg()->builder()->CreateUDiv(op1, cg()->llvmConstInt(1000000000, 1e9));
    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::Eq* i)
{
    auto op1 = cg()->llvmValue(i->op1());
    auto op2 = cg()->llvmValue(i->op2());

    auto result = builder()->CreateICmpEQ(op1, op2);

    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::Gt* i)
{
    auto op1 = cg()->llvmValue(i->op1());
    auto op2 = cg()->llvmValue(i->op2());

    auto result = builder()->CreateICmpSGT(op1, op2);
    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::Lt* i)
{
    auto op1 = cg()->llvmValue(i->op1());
    auto op2 = cg()->llvmValue(i->op2());

    auto result = builder()->CreateICmpSLT(op1, op2);
    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::Nsecs* i)
{
    CodeGen::expr_list args;
    args.push_back(i->op1());

    auto result = cg()->llvmCall("hlt::interval_nsecs", args);

    cg()->llvmStore(i, result);
}

void StatementBuilder::visit(statement::instruction::interval::Sub* i)
{
    auto op1 = cg()->llvmValue(i->op1());
    auto op2 = cg()->llvmValue(i->op2());

    auto result = builder()->CreateSub(op1, op2);

    cg()->llvmStore(i, result);
}
