#include "../include/driver_structure.hpp"


CDummyDriver::CDummyDriver(char* confFile,
                         unsigned short val_nZone,
                         SU2_Comm MPICommunicator) : CDriver(confFile,
                                                               val_nZone,
                                                               MPICommunicator) {
  
  
}


void CDummyDriver::StartSolver(){
  if (rank == MASTER_NODE){
    cout << endl <<"------------------------------ Begin Solver -----------------------------" << endl;
    cout << endl;
    cout << "--------------------------------------------" << endl;
    cout << "No solver started. DRY_RUN option enabled. " << endl;
    cout << "--------------------------------------------" << endl;
  }
}
