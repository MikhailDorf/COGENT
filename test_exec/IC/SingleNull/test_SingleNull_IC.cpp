#include "ParmParse.H"

#define CH_SPACEDIM CFG_DIM

#include "SingleNullTest.H"
#include "parstream.H"
#ifdef CH_MPI
#include "CH_Attach.H"
#endif

#include "UsingNamespace.H"

inline int checkCommandLineArgs( int a_argc, char* a_argv[] )
{
   // Check for an input file
   if (a_argc<=1) {
      pout() << "Usage:  test_SingleNull_IC...ex <inputfile>" << endl;
      pout() << "No input file specified" << endl;
      return -1;
   }
   return 0;
}


int main( int a_argc, char* a_argv[] )
{
#ifdef CH_MPI
   // Start MPI
   MPI_Init( &a_argc, &a_argv );
   setChomboMPIErrorHandler();
#endif

   const int status( checkCommandLineArgs( a_argc, a_argv ) );

   if (status==0) {
      ParmParse pp( a_argc-2, a_argv+2, NULL, a_argv[1] );
      SingleNullTest at( pp );
      at.initialize();
      at.writePlotFile( "single_null_ic_test" );
   }

#ifdef CH_MPI
   MPI_Finalize();
#endif

   return status;
}

