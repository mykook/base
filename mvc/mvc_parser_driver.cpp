/**
 * @file mvc_parser_driver.cpp
 */
#include <cstdio>
#include <cstring>
#include <cerrno>
#include "mvc_module.h"
#include "mvc_parser_driver.h"
#include "mvc_scan.hpp"           /* generated by flex */


extern FILE *yyin;

namespace mvc {

ParserDriver::ParserDriver() 
  : _source(SCT_NONE), _result(0), _traceScanner(false), _traceParser(false)
{
}

ParserDriver::~ParserDriver()
{
}

Module *ParserDriver::parse(SourceTag source, const std::string& file)
{
  _source = source;
  _file = file;
  
  /* setup scanner */
  YY_BUFFER_STATE yy_strbuf;
  switch (_source) {
  case SCT_FILE:
    if (!(yyin = fopen(_file.c_str(), "r"))) {
      error("Failed to open " + _file + ": " + strerror(errno));
      return NULL;
    }
    break;
  case SCT_STDIN:
    yyin = stdin;
    break;
  case SCT_STR:
    printf("STR: %s\n", _file.c_str());
    yy_strbuf = yy_scan_string(_file.c_str());
    yy_switch_to_buffer(yy_strbuf);
    break;
  default:
    assert(0);
    break;
  }

  /* create module to be populated */
  ModuleMgr *mmgr = ModuleMgr::getInstance();
  if ((_module = mmgr->createModule()) == NULL)
    return NULL;
  
  /* parse */
  yy::Parser parser(*this);
  _result = parser.parse();

  /* shutdown scanner */
  switch (_source) {
  case SCT_FILE:
    fclose(yyin);
    break;
  case SCT_STDIN:
    break;
  case SCT_STR:
    yy_delete_buffer(yy_strbuf);
    break;
  default:
    assert(0);
    break;
  }

  return _module;
}

void ParserDriver::error(const yy::location& l, const std::string& m)
{
  std::cerr << l << ": " << m << std::endl;
}

void ParserDriver::error(const std::string& m)
{
  std::cerr << m << std::endl;
}

} /* mvc */
