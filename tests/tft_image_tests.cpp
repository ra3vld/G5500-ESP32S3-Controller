#include "tft_image_rows.hpp"
#include <cassert>
#include <iostream>
#include <cstring>
int main(int argc,char **argv) {
  const bool binary=argc==2 && std::strcmp(argv[1],"--pixels")==0;
  uint32_t hash=2166136261u;unsigned rows=0,pixels=0;
  assert(forkrot::drawTftImageRows([&](unsigned y,uint16_t *row) {
    assert(y==rows++);
    for(unsigned i=0;i<forkrot::CAT_IMAGE_W;++i) {
      hash=(hash^uint8_t(row[i]))*16777619u;
      hash=(hash^uint8_t(row[i]>>8))*16777619u;
      if(binary) {const char bytes[]={char(row[i]),char(row[i]>>8)};std::cout.write(bytes,2);}
      ++pixels;
    }
  }));
  if(!binary) std::cout << rows << ' ' << pixels << ' ' << hash << '\n';
}
