
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_classpath_jdwp_value_BooleanValue__
#define __gnu_classpath_jdwp_value_BooleanValue__

#pragma interface

#include <gnu/classpath/jdwp/value/Value.h>
extern "Java"
{
  namespace gnu
  {
    namespace classpath
    {
      namespace jdwp
      {
        namespace value
        {
            class BooleanValue;
        }
      }
    }
  }
}

class gnu::classpath::jdwp::value::BooleanValue : public ::gnu::classpath::jdwp::value::Value
{

public:
  BooleanValue(jboolean);
  jboolean getValue();
public: // actually protected
  ::java::lang::Object * getObject();
  void write(::java::io::DataOutputStream *);
public: // actually package-private
  jboolean __attribute__((aligned(__alignof__( ::gnu::classpath::jdwp::value::Value)))) _value;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_classpath_jdwp_value_BooleanValue__
