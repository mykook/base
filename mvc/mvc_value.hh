/**
 * @file mvc_value.hh
 *
 * @brief Interface to values which represent compile-time objects such
 * as functions, properties, etc.
 */
#ifndef MVC_VALUE_HH
#define MVC_VALUE_HH

namespace mvc {

enum ValueTag {
  VT_PROP,
  VT_FUNC,
  VT_EVENT,
  VT_REACTOR,
  VT_NTAGS       /* number of tags */
};

/**
 * @class Value
 */
class Value {
public:
  Value(ValueTag t) : _tag(t) { }
  virtual ~Value() { }
  
  ValueTag getTag() { return _tag; }

protected:
  ValueTag _tag;
};

/**
 * @class Prop
 */
class Prop : public Value {
public:
  Prop(const std::string& name) : Value(VT_PROP), _name(name) { }
  ~Prop() { }

  std::string& getName() { return _name; }
  
private:
  std::string _name;
};

/**
 * @class Event
 */
class Event : public Value {
public:
  Event(const std::string& name) : Value(VT_EVENT), _name(name) { }
  ~Event() { }
  
  std::string& getName() { return _name; }
  
private:
  std::string _name;
};

/**
 * @class Function
 */
class Function : public Value {
public:
  Function(const std::string& name) : Value(VT_FUNC), _name(name) { }
  ~Function() { }
  
  std::string& getName() { return _name; }
  
private:
  std::string _name;
  unsigned _nargs;
};

/**
 * @class Reactor
 */
class Reactor : public Value {
public:
  Reactor(const std::string& name) : Value(VT_REACTOR), _name(name) { }
  ~Reactor() { }
  
  std::string& getName() { return _name; }
  
private:
  std::string _name;
};
  
} /* mvc */

#endif /* MVC_VALUE_HH */
