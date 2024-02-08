// Copyright (c) 2020-2023 by the Zeek Project. See LICENSE for details.

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <hilti/ast/declarations/parameter.h>
#include <hilti/ast/expression.h>
#include <hilti/ast/type.h>

namespace hilti::type {

namespace operand_list {

/** Base class for operand nodes. */
class Operand final : public Node {
public:
    const auto& id() const { return _id; }
    auto type() const { return child<QualifiedType>(0); }
    auto kind() const { return _kind; }
    auto isOptional() const { return _optional; }
    auto default_() const { return child<Expression>(1); }
    auto doc() const { return _doc; }

    node::Properties properties() const final {
        auto p = node::Properties{{"id", _id}, {"optional", _optional}, {"kind", to_string(_kind)}, {"doc", _doc}};
        return Node::properties() + p;
    }

    static auto create(ASTContext* ctx, parameter::Kind kind, const UnqualifiedTypePtr& type, bool optional = false,
                       std::string doc = "", Meta meta = {}) {
        return std::shared_ptr<Operand>(new Operand(ctx, {_makeOperandType(ctx, kind, type), nullptr}, {}, kind,
                                                    optional, std::move(doc), std::move(meta)));
    }

    static auto create(ASTContext* ctx, ID id, parameter::Kind kind, const UnqualifiedTypePtr& type,
                       bool optional = false, std::string doc = "", Meta meta = {}) {
        return std::shared_ptr<Operand>(new Operand(ctx, {_makeOperandType(ctx, kind, type), nullptr}, std::move(id),
                                                    kind, optional, std::move(doc), std::move(meta)));
    }

    static auto create(ASTContext* ctx, ID id, parameter::Kind kind, const UnqualifiedTypePtr& type,
                       const ExpressionPtr& default_, std::string doc = "", const Meta& meta = {}) {
        return std::shared_ptr<Operand>(new Operand(ctx, {_makeOperandType(ctx, kind, type), default_}, std::move(id),
                                                    kind, (default_ != nullptr), std::move(doc), meta));
    }

    static auto create(ASTContext* ctx, ID id, parameter::Kind kind, const UnqualifiedTypePtr& type,
                       const ExpressionPtr& default_, bool optional, std::string doc = "", const Meta& meta = {}) {
        return std::shared_ptr<Operand>(new Operand(ctx, {_makeOperandType(ctx, kind, type), default_}, std::move(id),
                                                    kind, optional, std::move(doc), meta));
    }

protected:
    Operand(ASTContext* ctx, Nodes children, ID id, parameter::Kind kind, bool optional, std::string doc,
            Meta meta = {})
        : Node(ctx, std::move(children), std::move(meta)),
          _id(std::move(id)),
          _kind(kind),
          _optional(optional),
          _doc(std::move(doc)) {}

    HILTI_NODE(hilti, Operand);

private:
    static QualifiedTypePtr _makeOperandType(ASTContext* ctx, parameter::Kind _kind, const UnqualifiedTypePtr& type);

    ID _id;
    parameter::Kind _kind = parameter::Kind::Unknown;
    bool _optional = false;
    std::string _doc;
};

using OperandPtr = std::shared_ptr<Operand>;
using Operands = std::vector<OperandPtr>;
} // namespace operand_list

/**
 * AST node for a type representing a list of function/method operands. This
 * is an internal type used for overload resolution, it's nothing actually
 * instantiated by a HILTI program. That's also why we don't use any child
 * nodes, but store the operands directly.
 */
class OperandList final : public UnqualifiedType {
public:
    auto operands() const { return children<operand_list::Operand>(0, {}); }
    auto op0() const {
        assert(children().size() >= 1);
        return child<operand_list::Operand>(0);
    }
    auto op1() const {
        assert(children().size() >= 2);
        return child<operand_list::Operand>(1);
    }
    auto op2() const {
        assert(children().size() >= 3);
        return child<operand_list::Operand>(2);
    }

    std::string_view typeClass() const final { return "operand-list"; }

    static auto create(ASTContext* ctx, operand_list::Operands operands, Meta meta = {}) {
        return std::shared_ptr<OperandList>(new OperandList(ctx, node::flatten(std::move(operands)), std::move(meta)));
    }

    static auto create(ASTContext* ctx, Wildcard _, const Meta& m = Meta()) {
        return std::shared_ptr<OperandList>(new OperandList(ctx, Wildcard(), m));
    }

    template<typename Container>
    static UnqualifiedTypePtr fromParameters(ASTContext* ctx, const Container& params) {
        operand_list::Operands ops;

        for ( const auto& p : params )
            ops.push_back(
                operand_list::Operand::create(ctx, p->id(), p->kind(), p->type()->type(false), p->default_()));

        return type::OperandList::create(ctx, std::move(ops));
    }

protected:
    OperandList(ASTContext* ctx, Nodes children, Meta meta)
        : UnqualifiedType(ctx, {}, std::move(children), std::move(meta)) {}
    OperandList(ASTContext* ctx, Wildcard _, const Meta& meta)
        : UnqualifiedType(ctx, Wildcard(), {"operand-list(*)"}, meta) {}

    HILTI_NODE(hilti, OperandList)
};

} // namespace hilti::type
