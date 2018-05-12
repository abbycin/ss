/***********************************************
        File Name: signal_slot.h
        Author: Abby Cin
        Mail: abbytsing@gmail.com
        Created Time: 5/12/18 2:56 PM
***********************************************/

#include <iostream>
#include "signal_slot.h"

template<typename Obj>
using str_t = void(Obj::*)(const std::string&, const std::string&);

template<typename Obj>
using num_t = void(Obj::*)(int, int);

struct Foo : nm::Object
{
  Foo(nm::App& app)
    : app_{app}
  {}
  void add(int lhs, int rhs)
  {
    log_debug() << lhs + rhs;
  }

  void add(const std::string& lhs, const std::string& rhs)
  {
    log_debug() << (lhs + rhs);
    app_.quit();
  }

  nm::App& app_;
};

struct Bar : nm::Object
{
  void add(int lhs, int rhs)
  {
    this->notify(this, static_cast<num_t<Bar>>(&Bar::add), lhs, rhs);
  }

  void add(const std::string& lhs, const std::string& rhs)
  {
    this->notify(this, static_cast<str_t<Bar>>(&Bar::add), lhs, rhs);
  }
};

int main()
{
  setenv("LOG_TYPE", "custom", 1);
  nm::App app;

  Foo* foo = new Foo{app};
  Bar* bar = new Bar;
  bar->post(bar, static_cast<num_t<Bar>>(&Bar::add), foo, static_cast<num_t<Foo>>(&Foo::add));
  bar->post(bar, static_cast<str_t<Bar>>(&Bar::add), foo, static_cast<str_t<Foo>>(&Foo::add));

  sleep(1);
  bar->add(1, 2);
  bar->add("hello ", "world");
  app.exec();
  sleep(1);

  delete foo;
  delete bar;
}
