/**
 *  @file test_compress_ts.c
 *  @author Sheng Di
 *  @date May, 2018
 *  @brief This is an example of using compression interface
 *  (C) 2015 by Mathematics and Computer Science (MCS), Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */


#include <stdio.h>
#include <stdlib.h>
#include "sz.h"
#include "rw.h"

struct timeval startTime;
struct timeval endTime;  /* Start and end times */
struct timeval costStart; /*only used for recording the cost*/
double totalCost = 0;
double all_snap_time = 0.0; //sihuan added
#define NB_variable 6

void cost_start()
{
	totalCost = 0;
        gettimeofday(&costStart, NULL);
}

void cost_end()
{
        double elapsed;
        struct timeval costEnd;
        gettimeofday(&costEnd, NULL);
        elapsed = ((costEnd.tv_sec*1000000+costEnd.tv_usec)-(costStart.tv_sec*1000000+costStart.tv_usec))/1000000.0;
        totalCost += elapsed;
}


int main(int argc, char * argv[])
{

	int	total_snap = 100;
    int i = 0;
    size_t r5=0,r4=0,r3=0,r2=0,r1=0;
    char cmprFilePath[640], outputDir[640], outputFilePath[600];
    int status = 0;
    float eb = 0.001;
    float eb2 = 0.01;
    
    if(argc < 3)
    {
		printf("Test case: testfloat_decompress_ts [srcDir] [dimension sizes...] [eb for position] [PW_REL eb for velocity] [start snapshot number] [end snapshot number] [vlct 1 for yes 0 for no]\n");
		printf("Example: testfloat_decompress_ts /home/sdi/Data/Hurricane-ISA/consecutive-steps 500 500 100\n");
		exit(0);
    }
  
    sprintf(outputDir, "%s", argv[1]);
	sprintf(global_dir, "%s", argv[1]);
    if(strcmp(outputDir, "sz.config")==0)
    {
    	printf("Error: wrong input\n");
	printf("Test case: testfloat_decompress_ts [srcDir] [dimension sizes...]\n");//add a comment to test github
	exit(0);
    }
	int i_start = 0; 
	int i_end = total_snap;
	int Snap_interval = 10;
    if(argc>=3)
		r1 = atoi(argv[2]); //8
    if(argc>=4)
		//r2 = atoi(argv[3]); //8
        eb = atof(argv[3]);
    if(argc>=5)
		//r3 = atoi(argv[4]); //128
        eb2 = atof(argv[4]);
    if(argc>=6)
        //r4 = atoi(argv[5]);
        i_start = atoi(argv[5]);
    if(argc>=7)
        //r5 = atoi(argv[6]);
	i_end = atoi(argv[6]);
	//if (argc>=8)
	//	Snap_interval = atoi(argv[7]);
	if (argc>=8)
		vlct = atoi(argv[7]);
      
    char oriFilePath[600];
    size_t byteLen = 0;
    size_t dataLength = computeDataLength(r5,r4,r3,r2,r1);
    //float *data = (float*)malloc(sizeof(float)*dataLength);
    float **data = (float**) malloc(NB_variable * sizeof(float*));
    for (i = 0; i < NB_variable; i++){
        data[i] = (float*) malloc((dataLength+dataLength/mem_over) * sizeof(float));
    }
    //SZ_registerVar("CLOUDf", SZ_FLOAT, data, REL, 0, 0.001, 0, r5, r4, r3, r2, r1);
    SZ_registerVar("x", SZ_FLOAT, data[0], REL, 0, eb, 0, r5, r4, r3, r2, r1);
    SZ_registerVar("y", SZ_FLOAT, data[1], REL, 0, eb, 0, r5, r4, r3, r2, r1);
    SZ_registerVar("z", SZ_FLOAT, data[2], REL, 0, eb, 0, r5, r4, r3, r2, r1);
    SZ_registerVar("vx", SZ_FLOAT, data[3], PW_REL, 0, 0, eb2, r5, r4, r3, r2, r1);
    SZ_registerVar("vy", SZ_FLOAT, data[4], PW_REL, 0, 0, eb2, r5, r4, r3, r2, r1);
    SZ_registerVar("vz", SZ_FLOAT, data[5], PW_REL, 0, 0, eb2, r5, r4, r3, r2, r1);
    //SZ_registerVar("vx", SZ_FLOAT, data[3], REL, 0, eb2, 0, r5, r4, r3, r2, r1);
    //SZ_registerVar("vy", SZ_FLOAT, data[4], REL, 0, eb2, 0, r5, r4, r3, r2, r1);
    //SZ_registerVar("vz", SZ_FLOAT, data[5], REL, 0, eb2, 0, r5, r4, r3, r2, r1);
    //SZ_registerVar("index", SZ_INT64, index, REL, 0, 0.001, 0, r5, r4, r3, r2, r1);


    if(status != SZ_SCES)
    {
		printf("Error: data file %s cannot be read!\n", oriFilePath);
		exit(0);
    }
    //size_t delta_t_num;
    sprintf(cmprFilePath, "%s/delta_t_opt_output.txt", outputDir);
    float* tmp_delta_t = (float*) malloc(sizeof(float)*total_snap);
    FILE* myfile;
    myfile = fopen(cmprFilePath, "r");
    for (i = 0; i < total_snap; i++){
        fscanf(myfile, "%f", &tmp_delta_t[i]);
    }
	fclose(myfile);

    //tmp_delta_t = readFloatData(cmprFilePath, &delta_t_num, &status);
    printf("read delta time from its output file:\n");
    for (i = 0; i < total_snap; i++){
        printf("%.10f  ", tmp_delta_t[i]);
        delta_t_opt[i] = tmp_delta_t[i];
    }
    printf("\n");
    //return 0;
   
    for(i=i_start;i<i_end;i++)
	{
		printf("simulation time step %d\n", i);
		sprintf(cmprFilePath, "%s/QCLOUDf%02d.bin.dat.sz2", outputDir, i);
		unsigned char *bytes = readByteData(cmprFilePath, &byteLen, &status);
        //sihuan debug
        printf("it goes through readByteData\n");
		cost_start();
		SZ_decompress_ts_vlct(bytes, byteLen);
		//all_snap_time += totalCost;
		cost_end();
		all_snap_time += totalCost;
		printf("timecost=%f\n",totalCost);
	free(bytes);
        int m;
        for (m = 0; m < 6; m++){
            sprintf(outputFilePath, "%s/QCLOUDf%d-%d.bin.dat.sz2.out", outputDir, i, m);
            printf("writing decompressed data to %s\n", outputFilePath);
            //writeFloatData_inBytes(data[m], dataLength, outputFilePath, &status);// how to manage multi variables in data[]?
            writeFloatData_inBytes(data[m], sz_varset->header->next->r1, outputFilePath, &status);//sihuan updated
        }
	//	free(bytes);
	}
	printf("GREPdec time is: %f\n", all_snap_time);
	sprintf(outputFilePath, "%s/decmpr_total_snap_time_%f_%f.txt", outputDir, eb, eb2);
	writeDoubleData(&all_snap_time, 1, outputFilePath, &status);//sihuan added

    
    printf("done\n");
	for (i = 0; i < NB_variable; i++){
		free(data[NB_variable-1-i]);
	}
    free(data);
    SZ_Finalize();
    
    return 0;
}
