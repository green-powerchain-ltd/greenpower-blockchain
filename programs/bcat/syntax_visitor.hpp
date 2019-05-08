//
// Created by damir on 3.11.17..
//

#pragma once

#include <fc/variant.hpp>

class syntax_visitor : public fc::variant::visitor
{
public:
  explicit syntax_visitor(fc::variants syntax)
          : m_syntax(std::move(syntax))
          , m_cnt(0)
  {}

  void handle() const override
  {}

  void handle(const int64_t &v) const override
  {
    handle_impl("integer");
  }

  void handle(const uint64_t &v) const override
  {
    handle_impl("integer");
  }

  void handle(const double &v) const override
  {
    handle_impl("double");
  }

  void handle(const bool &v) const override
  {
    handle_impl("bool");
  }

  void handle(const fc::string &v) const override
  {
    handle_impl("string");
  }

  void handle(const fc::variant_object &v) const override
  {
    handle_impl("object");
  }

  void handle(const fc::variants &v) const override
  {
    handle_impl("array");
  }

private:
  void handle_impl(const std::string &type) const
  {
    if (m_syntax[m_cnt].as_string() != type)
      throw std::runtime_error("expected " + m_syntax[m_cnt].as_string() + ", got " + type);
    ++m_cnt;
  }

  fc::variants m_syntax;
  mutable unsigned int m_cnt;
};
