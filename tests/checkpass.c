#include <stdio.h>
#include <string.h>
#include <stdlib.h>


int main(int argc, char *argv[])
{
    FILE *Fichier;
    int res = 1;
    char motFr[60], motR[60] = " collective(s) found";
    Fichier = fopen(argv[1], "r");
    if (!Fichier){
         //printf("\aERREUR: Impossible d'ouvrir le fichier: %s.\n", argv[1]);
         res = 1;
         printf("%d\n", res);
         return res;
       }
    //scanf("%s",motR);
    while (fgets(motFr,60 ,Fichier) != NULL)
    {
      if (strstr(motFr, motR) != NULL){
          res = 0;
        }
    }
    fclose(Fichier);
    //printf("%d\n", res);
    return res;
}
