#include "trama/trama.hpp"
int main(int argc,char** argv){try{return trama::cli(argc,argv);}catch(const std::exception& e){fprintf(stderr,"trama: %s\n",e.what());return 1;}}
