#include "qif_model.hpp"
#include <iostream>
#include <string>
int main(int argc,char**argv){
  if(argc!=2){std::cerr<<"usage: sheet_surface_load_test file.qif\n";return 2;}
  qifv::QifMesh mesh;std::string error;qifv::LoadOptions opts;
  if(!qifv::QifLoader::load(argv[1],mesh,error,opts)){std::cerr<<error<<"\n";return 1;}
  if(mesh.faceCount!=1||mesh.triangles.empty()){std::cerr<<"sheet face was not tessellated\n";return 1;}
  if(!mesh.bounds.valid || mesh.bounds.max.x < 4.9 || mesh.bounds.min.x > -4.9 || mesh.bounds.max.y < 4.9 || mesh.bounds.min.y > -4.9){
    std::cerr<<"sheet-circle bounds are incomplete\n";return 1;
  }
  std::cout<<"sheet body / circular edge without Curve12: PASS ("<<mesh.triangles.size()<<" triangles)\n";
  return 0;
}
