#include <iostream>

#include <boost/lexical_cast.hpp>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

extern "C" {
  void test_c();
}

void test_c()
{
  std::cout << "C++" << std::endl;
}
