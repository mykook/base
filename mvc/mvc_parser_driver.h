/**
 * @file mvc_parser_driver.h
 *
 * @brief Driver class for the EPL parser and the EPL scanner. Based
 * on the example in Chapter 10 of the Bison manual.
 */
#ifndef MVC_PARSER_DRIVER_H
#define MVC_PARSER_DRIVER_H

#include <list>
#include "mvc_module.h"
#include "mvc_parse.hpp"       /* generated by bison */

namespace mvc {

/* Declaration of scanning function. Flex expects the signature of yylex 
   in the YY_DECL, and the C++ parser expects it to be declared. 
   We can factor both as follows ... */
#define YY_DECL                                       \
  extern "C" mvc::yy::Parser::token_type              \
  yylex(mvc::yy::Parser::semantic_type *yylval,       \
        mvc::yy::Parser::location_type *yylloc,       \
        mvc::ParserDriver& driver)

/* ... and declare it for the parser's sake. */
YY_DECL;

/**
 * @class ParserDriver
 *
 * @brief Responsible for conducting the scanning and parsing of MVC code.
 * This object will be used by two major objects: Parser (generated by
 * bison) and ParserServer.
 *
 *    Parser <---<mvc code>--- ParserDriver <---<mvc code>--- ParserServer
 *      |
 *      +------<Stm list>----> ParserDriver ---<Stm list>---> ParserServer
 *
 * @TODO Make multiple parser threads and dispatch parsing jobs concurrently.
 */
class ParserDriver {
public:
  ParserDriver();
  virtual ~ParserDriver();
  
  /* Functions for parsing. Pass either filename or the string itself
     to be parsed in str.
  
     TODO: Return ParseResult object, which consists of result code and
     Stm object, or else. */
  Module *parse(SourceTag source, const std::string& str);
  std::string& getFile() { return _file; }

  Module *getModule() { return _module; }

  /* Error handling. */
  void error(const yy::location& l, const std::string& m);
  void error(const std::string& m);

private:
  SourceTag _source;
  std::string _file;
  Module *_module;

  int _result;

  bool _traceScanner;
  bool _traceParser;
};

} /* mvc */

#endif /* MVC_PARSE_DRIVER_H */