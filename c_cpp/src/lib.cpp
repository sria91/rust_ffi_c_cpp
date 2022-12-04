#include <sstream>
#include "lib.h"

std::stringstream ss;

const char * introduce(const char * name, int age) {
    ss.clear();
    ss << "Hi, I am " << name << ". My age is " << age << ".";
    return strdup(ss.str().c_str());
}
