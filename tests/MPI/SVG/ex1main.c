#include <stdio.h>
#include "mpi.h"

#define MAXSIZE 100

void g(int s){
  if(s > MAXSIZE)
        MPI_Barrier(MPI_COMM_WORLD);
}

void b(){
	MPI_Barrier(MPI_COMM_WORLD);

}

void f(){

  int i=0, s=0, r=0;

  MPI_Comm_size(MPI_COMM_WORLD,&s);
  MPI_Comm_rank(MPI_COMM_WORLD,&r);

	printf("Hello I am P%d\n",r);

//if(r>3){
 if((r+1)%2){
  MPI_Barrier(MPI_COMM_WORLD);
	 //h(s);
 }else{
  MPI_Barrier(MPI_COMM_WORLD);
//	h(s);
 }
//}


if(r%2){
//	h(s);
  MPI_Barrier(MPI_COMM_WORLD);
}

/* for(i=0; i<5; i++){
	 h(s);
 }
*/

}

int main(int argc, char **argv){

  MPI_Init(&argc,&argv);
  f();
  MPI_Finalize();
  return 0;
}
