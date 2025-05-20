#include <iostream>
#include <map>

int main() { //
  std::string name = "test";
  std::map<int, std::string> map{{1, "eins"}, {2, "zwei"}, {3, "drei"}};
  std::cout << "Hello, " << name << "!" << '\n';
}
