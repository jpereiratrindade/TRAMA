#include "trama/trama.hpp"
int main(int argc,char** argv){try{return trama::server_main(argc,argv);}catch(const std::exception& e){fprintf(stderr,"trama-server: %s\n",e.what());return 1;}}
