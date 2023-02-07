#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "omp.h"



int iEbarrier, iIbarrier, iSingle, iSection, iFor, iReturn;


int is_incorrect(){
	return (iReturn!=omp_get_num_threads() &&
			(iEbarrier + iIbarrier) !=omp_get_num_threads() &&
			iSingle!=omp_get_num_threads() &&
			iSection!=omp_get_num_threads() &&
			iFor!=omp_get_num_threads());
}


/* Check Collective OMP Function   
 *
 *  color = type of collective (unique per collective)
 *  OP_name = collective name 
 *  OP_line = line in the source code of the collective 
 *  warnings = warnings emitted at compile-time
 *  FILE_name = name of the file
 */
void check_collective_OMP(int OP_color, const char* OP_name, int OP_line, char* warnings, char *FILE_name)
{

#ifdef DEBUG
	fprintf(stderr,"T%d has color %d\n",omp_get_thread_num(),OP_color);
#endif

	#pragma omp master
	{
		// return, explicit & implicit barriers, single, section, for
		iReturn=0; iEbarrier=0; iIbarrier=0; iSingle=0; iSection=0; iFor=0; 
	}
	#pragma omp barrier

	if(omp_in_parallel()==0 || omp_get_num_threads()==1)
		return;

	switch(OP_color){
		case 36:
			#pragma omp atomic
			iEbarrier++;
			break;
		case 1:
			#pragma omp atomic
			iIbarrier++;
			break;
		case 38:
			#pragma omp atomic
			iReturn++;
			break;
		case 3:
			#pragma omp atomic
			iSingle++;
			break;
		case 4:
			#pragma omp atomic
			iSection++;
			break;
		case 5:
			#pragma omp atomic
			iFor++;
			break;
		default:
			break;

	}

	#pragma omp barrier

	#pragma omp master
	{
		if(is_incorrect()){
			printf("\033[0;35m PARCOACH DYNAMIC-CHECK : Error detected \033[0;0m\n");
			printf("   \033[0;36m Number of threads before Single %d | E. barrier + I. barrier %d | Sections %d | For %d | Return %d \033[0;0m\n",
					iSingle, iEbarrier+iIbarrier, iSection, iFor, iReturn);
			printf("\033[0;36m Abort is invoking \033[0;0m\n ");
			abort();
		}
	}

	#pragma omp barrier
}

void check_collective_return(int OP_color, const char* OP_name, int OP_line, char* warnings, char *FILE_name)
{
#ifdef DEBUG
	fprintf(stderr,"T%d has color %d\n",omp_get_thread_num(),OP_color);
#endif
	#pragma omp master
	{
		// return, explicit & implicit barriers, single, section, for
		iReturn=0; iEbarrier=0; iIbarrier=0; iSingle=0; iSection=0; iFor=0; 
	}
	#pragma omp barrier

	if(omp_in_parallel()==0 || omp_get_num_threads()==1)
		return;

	switch(OP_color){
		case 36:
			#pragma omp atomic
			iEbarrier++;
			break;
		case 1:
			#pragma omp atomic
			iIbarrier++;
			break;
		case 38:
			#pragma omp atomic
			iReturn++;
			break;
		case 3:
			#pragma omp atomic
			iSingle++;
			break;
		case 4:
			#pragma omp atomic
			iSection++;
			break;
		case 5:
			#pragma omp atomic
			iFor++;
			break;
		default:
			break;

	}

	#pragma omp barrier

	#pragma omp master
	{
		if(is_incorrect()){
			printf("\033[0;35m PARCOACH DYNAMIC-CHECK : Error detected \033[0;0m\n");
			printf("   \033[0;36m Number of threads before Single %d | E. barrier + I. barrier %d | Sections %d | For %d | Return %d \033[0;0m\n",
					iSingle, iEbarrier+iIbarrier, iSection, iFor, iReturn);
			printf("\033[0;36m Abort is invoking \033[0;0m\n ");
			abort();
		}
	}

	#pragma omp barrier
}
