/**
 *  @file sz_float.c
 *  @author Sheng Di and Dingwen Tao
 *  @date Aug, 2016
 *  @brief SZ_Init, Compression and Decompression functions
 *  (C) 2016 by Mathematics and Computer Science (MCS), Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "sz.h"
#include "CompressElement.h"
#include "DynamicByteArray.h"
#include "DynamicIntArray.h"
#include "TightDataPointStorageF.h"
#include "zlib.h"
#include "rw.h"
#include "sz_float_ts.h"

unsigned int optimize_intervals_float_1D_ts(float *oriData, size_t dataLength, float* preData, double realPrecision)
{	
	size_t i = 0, radiusIndex;
	float pred_value = 0, pred_err;
	size_t *intervals = (size_t*)malloc(confparams_cpr->maxRangeRadius*sizeof(size_t));
	memset(intervals, 0, confparams_cpr->maxRangeRadius*sizeof(size_t));
	size_t totalSampleSize = dataLength/confparams_cpr->sampleDistance;
	for(i=2;i<dataLength;i++)
	{
		if(i%confparams_cpr->sampleDistance==0)
		{
			pred_value = preData[i];
			pred_err = fabs(pred_value - oriData[i]);
			radiusIndex = (unsigned long)((pred_err/realPrecision+1)/2);
			if(radiusIndex>=confparams_cpr->maxRangeRadius)
				radiusIndex = confparams_cpr->maxRangeRadius - 1;			
			intervals[radiusIndex]++;
		}
	}
	//compute the appropriate number
	size_t targetCount = totalSampleSize*confparams_cpr->predThreshold;
	size_t sum = 0;
	for(i=0;i<confparams_cpr->maxRangeRadius;i++)
	{
		sum += intervals[i];
		if(sum>targetCount)
			break;
	}
	if(i>=confparams_cpr->maxRangeRadius)
		i = confparams_cpr->maxRangeRadius-1;
		
	unsigned int accIntervals = 2*(i+1);
	unsigned int powerOf2 = roundUpToPowerOf2(accIntervals);
	
	if(powerOf2<32)
		powerOf2 = 32;
	
	free(intervals);
	return powerOf2;
}

TightDataPointStorageF* SZ_compress_float_1D_MDQ_ts(float *oriData, size_t dataLength, sz_multisteps* multisteps,
double realPrecision, float valueRangeSize, float medianValue_f)
{
	float* preStepData = (float*)(multisteps->hist_data);

	//store the decompressed data
	float* decData = (float*)malloc(sizeof(float)*dataLength);
	memset(decData, 0, sizeof(float)*dataLength);
	
	unsigned int quantization_intervals;
	if(exe_params->optQuantMode==1)
		quantization_intervals = optimize_intervals_float_1D_ts(oriData, dataLength, preStepData, realPrecision);
	else
		quantization_intervals = exe_params->intvCapacity;
	updateQuantizationInfo(quantization_intervals);	

	size_t i;
	int reqLength;
	float medianValue = medianValue_f;
	short radExpo = getExponent_float(valueRangeSize/2);
	
	computeReqLength_float(realPrecision, radExpo, &reqLength, &medianValue);	

	int* type = (int*) malloc(dataLength*sizeof(int));
		
	float* spaceFillingValue = oriData; //
	
	DynamicIntArray *exactLeadNumArray;
	new_DIA(&exactLeadNumArray, DynArrayInitLen);
	
	DynamicByteArray *exactMidByteArray;
	new_DBA(&exactMidByteArray, DynArrayInitLen);
	
	DynamicIntArray *resiBitArray;
	new_DIA(&resiBitArray, DynArrayInitLen);
	
	unsigned char preDataBytes[4];
	intToBytes_bigEndian(preDataBytes, 0);
	
	int reqBytesLength = reqLength/8;
	int resiBitsLength = reqLength%8;

	FloatValueCompressElement *vce = (FloatValueCompressElement*)malloc(sizeof(FloatValueCompressElement));
	LossyCompressionElement *lce = (LossyCompressionElement*)malloc(sizeof(LossyCompressionElement));
				
	//add the first data	
	type[0] = 0;
	compressSingleFloatValue(vce, spaceFillingValue[0], realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
	updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
	memcpy(preDataBytes,vce->curBytes,4);
	addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
	decData[0] = vce->data;
		
	//add the second data
	type[1] = 0;
	compressSingleFloatValue(vce, spaceFillingValue[1], realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
	updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
	memcpy(preDataBytes,vce->curBytes,4);
	addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
	decData[1] = vce->data;	
	
	int state = 0;
	double checkRadius = 0;
	float curData = 0;
	float pred = 0;
	float predAbsErr = 0;
	checkRadius = (exe_params->intvCapacity-1)*realPrecision;
	double interval = 2*realPrecision;
	int j = 0;
	while (sz_tsc->bit_array[j] == '1'){j++;}
	j++;
	while (sz_tsc->bit_array[j] == '1'){j++;}
	j++;

	//for(i=2;i<dataLength && j < 418605;i++, j++)//sihuan update:
	for(i=2;i<dataLength;i++, j++)
	{
		while(sz_tsc->bit_array[j] == '1'){j++;}
		curData = spaceFillingValue[i];
		//pred = preStepData[i];
		pred = preStepData[j];
		predAbsErr = fabs(curData - pred);	
		if(predAbsErr<=checkRadius)
		{
			state = (predAbsErr/realPrecision+1)/2;
			if(curData>=pred)
			{
				type[i] = exe_params->intvRadius+state;
				pred = pred + state*interval;
			}
			else //curData<pred
			{
				type[i] = exe_params->intvRadius-state;
				pred = pred - state*interval;
			}
				
			//double-check the prediction error in case of machine-epsilon impact	
			if(fabs(curData-pred)>realPrecision)
			{	
				type[i] = 0;				
				compressSingleFloatValue(vce, curData, realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
				updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
				memcpy(preDataBytes,vce->curBytes,4);
				addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);		
				decData[i] = vce->data;
			}
			else
			{
				decData[i] = pred;
			}
			
			continue;
		}
		
		//unpredictable data processing		
		type[i] = 0;		
		compressSingleFloatValue(vce, curData, realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
		updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
		memcpy(preDataBytes,vce->curBytes,4);
		addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
		decData[i] = vce->data;
	}//end of for

	printf("after doing compression, the loop iterations, data length are: %zu, %zu\n", i, dataLength);
		
	size_t exactDataNum = exactLeadNumArray->size;
	
	TightDataPointStorageF* tdps;
			
	new_TightDataPointStorageF(&tdps, dataLength, exactDataNum, 
			type, exactMidByteArray->array, exactMidByteArray->size,  
			exactLeadNumArray->array,  
			resiBitArray->array, resiBitArray->size, 
			resiBitsLength,
			realPrecision, medianValue, (char)reqLength, quantization_intervals, NULL, 0, 0);

	//free memory
	free_DIA(exactLeadNumArray);
	free_DIA(resiBitArray);
	free(type);	
	free(vce);
	free(lce);	
	free(exactMidByteArray); //exactMidByteArray->array has been released in free_TightDataPointStorageF(tdps);
		
	memcpy(preStepData, decData, dataLength*sizeof(float)); //update the data
	free(decData);
	
	return tdps;
}


TightDataPointStorageF* SZ_compress_float_1D_MDQ_ts_hist_invlog(float *oriData, size_t dataLength, sz_multisteps* multisteps,
double realPrecision, float valueRangeSize, float medianValue_f, unsigned char* signs, float threshold)
{
	float* preStepData = (float*)(multisteps->hist_data);

	//store the decompressed data
	float* decData = (float*)malloc(sizeof(float)*dataLength);
	memset(decData, 0, sizeof(float)*dataLength);
	
	unsigned int quantization_intervals;
	if(exe_params->optQuantMode==1)
		quantization_intervals = optimize_intervals_float_1D_ts(oriData, dataLength, preStepData, realPrecision);
	else
		quantization_intervals = exe_params->intvCapacity;
	updateQuantizationInfo(quantization_intervals);	

	size_t i;
	int reqLength;
	float medianValue = medianValue_f;
	short radExpo = getExponent_float(valueRangeSize/2);
	//float threshold = -127 - 1.0001*realPrecision;
	
	computeReqLength_float(realPrecision, radExpo, &reqLength, &medianValue);	

	int* type = (int*) malloc(dataLength*sizeof(int));
		
	float* spaceFillingValue = oriData; //
	
	DynamicIntArray *exactLeadNumArray;
	new_DIA(&exactLeadNumArray, DynArrayInitLen);
	
	DynamicByteArray *exactMidByteArray;
	new_DBA(&exactMidByteArray, DynArrayInitLen);
	
	DynamicIntArray *resiBitArray;
	new_DIA(&resiBitArray, DynArrayInitLen);
	
	unsigned char preDataBytes[4];
	intToBytes_bigEndian(preDataBytes, 0);
	
	int reqBytesLength = reqLength/8;
	int resiBitsLength = reqLength%8;

	FloatValueCompressElement *vce = (FloatValueCompressElement*)malloc(sizeof(FloatValueCompressElement));
	LossyCompressionElement *lce = (LossyCompressionElement*)malloc(sizeof(LossyCompressionElement));
				
	//add the first data	
	type[0] = 0;
	compressSingleFloatValue(vce, spaceFillingValue[0], realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
	updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
	memcpy(preDataBytes,vce->curBytes,4);
	addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
	decData[0] = vce->data;
	if (decData[0] < threshold) decData[0] = 0;
	else decData[0] = exp2(decData[0]);
	if (signs[0]) decData[0] = -decData[0];
		
	//add the second data
	type[1] = 0;
	compressSingleFloatValue(vce, spaceFillingValue[1], realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
	updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
	memcpy(preDataBytes,vce->curBytes,4);
	addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
	decData[1] = vce->data;	
	if (decData[1] < threshold) decData[1] = 1;
	else decData[1] = exp2(decData[1]);
	if (signs[1]) decData[1] = -decData[1];
	
	int state = 0;
	double checkRadius = 0;
	float curData = 0;
	float pred = 0;
	float predAbsErr = 0;
	checkRadius = (exe_params->intvCapacity-1)*realPrecision;
	double interval = 2*realPrecision;
	int j = 0;
	while (sz_tsc->bit_array[j] == '1'){j++;}
	j++;
	while (sz_tsc->bit_array[j] == '1'){j++;}
	j++;
	int j_start = j;
	

	for (i = 2; i < dataLength; i++, j++){
		while(sz_tsc->bit_array[j] == '1'){j++;}
		if (fabs(preStepData[j]) > 0)
		preStepData[j] = log2(fabs(preStepData[j]));
	}
	j = j_start;


	//for(i=2;i<dataLength && j < 418605;i++, j++)//sihuan update:
	for(i=2;i<dataLength;i++, j++)
	{
		while(sz_tsc->bit_array[j] == '1'){j++;}
		curData = spaceFillingValue[i];
		//pred = preStepData[i];
		pred = preStepData[j];
		predAbsErr = fabs(curData - pred);	
		if(predAbsErr<=checkRadius)
		{
			state = (predAbsErr/realPrecision+1)/2;
			if(curData>=pred)
			{
				type[i] = exe_params->intvRadius+state;
				pred = pred + state*interval;
			}
			else //curData<pred
			{
				type[i] = exe_params->intvRadius-state;
				pred = pred - state*interval;
			}
				
			//double-check the prediction error in case of machine-epsilon impact	
			if(fabs(curData-pred)>realPrecision)
			{	
				type[i] = 0;				
				compressSingleFloatValue(vce, curData, realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
				updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
				memcpy(preDataBytes,vce->curBytes,4);
				addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);		
				decData[i] = vce->data;
				
					if (decData[i] < threshold) decData[i] = 0;
					else decData[i] = exp2(decData[i]);
					if (signs[i]) decData[i] = -decData[i];
				
			}
			else
			{
				decData[i] = pred;
				
					if (decData[i] < threshold) decData[i] = 0;
					else decData[i] = exp2(decData[i]);
					if (signs[i]) decData[i] = -decData[i];
				
			}
			
			continue;
		}
		
		//unpredictable data processing		
		type[i] = 0;		
		compressSingleFloatValue(vce, curData, realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
		updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
		memcpy(preDataBytes,vce->curBytes,4);
		addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
		decData[i] = vce->data;
		
			if (decData[i] < threshold) decData[i] = 0;
			else decData[i] = exp2(decData[i]);
			if (signs[i]) decData[i] = -decData[i];
		
	}//end of for

	printf("after doing compression, the loop iterations, data length are: %zu, %zu\n", i, dataLength);
		
	size_t exactDataNum = exactLeadNumArray->size;
	
	TightDataPointStorageF* tdps;
			
	new_TightDataPointStorageF(&tdps, dataLength, exactDataNum, 
			type, exactMidByteArray->array, exactMidByteArray->size,  
			exactLeadNumArray->array,  
			resiBitArray->array, resiBitArray->size, 
			resiBitsLength,
			realPrecision, medianValue, (char)reqLength, quantization_intervals, NULL, 0, 0);

	//free memory
	free_DIA(exactLeadNumArray);
	free_DIA(resiBitArray);
	free(type);	
	free(vce);
	free(lce);	
	free(exactMidByteArray); //exactMidByteArray->array has been released in free_TightDataPointStorageF(tdps);
		
	memcpy(preStepData, decData, dataLength*sizeof(float)); //update the data
	free(decData);
	
	return tdps;
}






TightDataPointStorageF* SZ_compress_float_1D_MDQ_ts_pwr_vlct(float *oriData, size_t dataLength, sz_multisteps* multisteps,
double realPrecision, float valueRangeSize, float medianValue_f, unsigned char* signs)
{
	float* preStepData = (float*)(multisteps->hist_data);

	//store the decompressed data
	float* decData = (float*)malloc(sizeof(float)*dataLength);
	memset(decData, 0, sizeof(float)*dataLength);
	
	unsigned int quantization_intervals;
	if(exe_params->optQuantMode==1)
		quantization_intervals = optimize_intervals_float_1D_ts(oriData, dataLength, preStepData, realPrecision);
	else
		quantization_intervals = exe_params->intvCapacity;
	updateQuantizationInfo(quantization_intervals);	

	size_t i;
	int reqLength;
	float medianValue = medianValue_f;
	short radExpo = getExponent_float(valueRangeSize/2);
	
	computeReqLength_float(realPrecision, radExpo, &reqLength, &medianValue);	

	int* type = (int*) malloc(dataLength*sizeof(int));
		
	float* spaceFillingValue = oriData; //
	
	DynamicIntArray *exactLeadNumArray;
	new_DIA(&exactLeadNumArray, DynArrayInitLen);
	
	DynamicByteArray *exactMidByteArray;
	new_DBA(&exactMidByteArray, DynArrayInitLen);
	
	DynamicIntArray *resiBitArray;
	new_DIA(&resiBitArray, DynArrayInitLen);
	
	unsigned char preDataBytes[4];
	intToBytes_bigEndian(preDataBytes, 0);
	
	int reqBytesLength = reqLength/8;
	int resiBitsLength = reqLength%8;

	FloatValueCompressElement *vce = (FloatValueCompressElement*)malloc(sizeof(FloatValueCompressElement));
	LossyCompressionElement *lce = (LossyCompressionElement*)malloc(sizeof(LossyCompressionElement));
				
	//add the first data	
	type[0] = 0;
	compressSingleFloatValue(vce, spaceFillingValue[0], realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
	updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
	memcpy(preDataBytes,vce->curBytes,4);
	addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
	decData[0] = vce->data;
		
	//add the second data
	type[1] = 0;
	compressSingleFloatValue(vce, spaceFillingValue[1], realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
	updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
	memcpy(preDataBytes,vce->curBytes,4);
	addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
	decData[1] = vce->data;	
	
	int state = 0;
	double checkRadius = 0;
	float curData = 0;
	float pred = 0;
	float predAbsErr = 0;
	checkRadius = (exe_params->intvCapacity-1)*realPrecision;
	double interval = 2*realPrecision;
	int j = 0;
	while (sz_tsc->bit_array[j] == '1'){j++;}
	j++;
	while (sz_tsc->bit_array[j] == '1'){j++;}
	j++;

	//for(i=2;i<dataLength && j < 418605;i++, j++)//sihuan update:
	for(i=2;i<dataLength;i++, j++)
	{
		while(sz_tsc->bit_array[j] == '1'){j++;}
		curData = spaceFillingValue[i];
		//pred = preStepData[i];
		pred = preStepData[j];
		predAbsErr = fabs(curData - pred);	
		if(predAbsErr<=checkRadius)
		{
			state = (predAbsErr/realPrecision+1)/2;
			if(curData>=pred)
			{
				type[i] = exe_params->intvRadius+state;
				pred = pred + state*interval;
			}
			else //curData<pred
			{
				type[i] = exe_params->intvRadius-state;
				pred = pred - state*interval;
			}
				
			//double-check the prediction error in case of machine-epsilon impact	
			if(fabs(curData-pred)>realPrecision)
			{	
				type[i] = 0;				
				compressSingleFloatValue(vce, curData, realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
				updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
				memcpy(preDataBytes,vce->curBytes,4);
				addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);		
				decData[i] = vce->data;
			}
			else
			{
				decData[i] = pred;
			}
			
			continue;
		}
		
		//unpredictable data processing		
		type[i] = 0;		
		compressSingleFloatValue(vce, curData, realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
		updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
		memcpy(preDataBytes,vce->curBytes,4);
		addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
		decData[i] = vce->data;
	}//end of for

	printf("after doing compression, the loop iterations, data length are: %zu, %zu\n", i, dataLength);
		
	size_t exactDataNum = exactLeadNumArray->size;
	
	TightDataPointStorageF* tdps;
			
	new_TightDataPointStorageF(&tdps, dataLength, exactDataNum, 
			type, exactMidByteArray->array, exactMidByteArray->size,  
			exactLeadNumArray->array,  
			resiBitArray->array, resiBitArray->size, 
			resiBitsLength,
			realPrecision, medianValue, (char)reqLength, quantization_intervals, NULL, 0, 0);

	//free memory
	free_DIA(exactLeadNumArray);
	free_DIA(resiBitArray);
	free(type);	
	free(vce);
	free(lce);	
	free(exactMidByteArray); //exactMidByteArray->array has been released in free_TightDataPointStorageF(tdps);
		
	memcpy(preStepData, decData, dataLength*sizeof(float)); //update the data
	free(decData);
	
	return tdps;
}





TightDataPointStorageF* SZ_compress_float_1D_MDQ_ts_vlct(float *oriData, size_t dataLength, sz_multisteps* multisteps,
double realPrecision, float valueRangeSize, float medianValue_f)
{
	float* preStepData = (float*)(multisteps->hist_data);
	SZ_Variable* v_tmp = NULL;
	v_tmp = sz_varset->header->next;
	if (!strcmp(v_global->varName, "x")){
		printf("vlct find vx for x\n");
		while(strcmp(v_tmp->varName, "vx")) {v_tmp = v_tmp->next;}
		printf("found varible name: %s\n", v_tmp->varName);
	}
	else if (!strcmp(v_global->varName, "y")){
		printf("vlct find vy for y\n");
		while(strcmp(v_tmp->varName, "vy")) {v_tmp = v_tmp->next;}
		printf("found varible name: %s\n", v_tmp->varName);
	}
	else if (!strcmp(v_global->varName, "z")){
		printf("vlct find vz for z\n");
		while(strcmp(v_tmp->varName, "vz")) {v_tmp = v_tmp->next;}
		printf("found varible name: %s\n", v_tmp->varName);
	}
	else printf("!!!!!!!!Should not call SZ_compress_float_1D_MDQ_ts_vlct, instead consider SZ_compress_float_1D_MDQ_ts!!!!!\n");

	float* preStepData_vol = NULL;
	if(v_tmp->errBoundMode == PW_REL) preStepData_vol = (float*)(v_tmp->multisteps->hist_invlog_data);
	else preStepData_vol = (float*)(v_tmp->multisteps->hist_data);

	//store the decompressed data
	float* decData = (float*)malloc(sizeof(float)*dataLength);
	memset(decData, 0, sizeof(float)*dataLength);
	
	unsigned int quantization_intervals;
	if(exe_params->optQuantMode==1)
		quantization_intervals = optimize_intervals_float_1D_ts(oriData, dataLength, preStepData, realPrecision);
	else
		quantization_intervals = exe_params->intvCapacity;
	updateQuantizationInfo(quantization_intervals);	

	size_t i;
	int reqLength;
	float medianValue = medianValue_f;
	short radExpo = getExponent_float(valueRangeSize/2);
	
	computeReqLength_float(realPrecision, radExpo, &reqLength, &medianValue);	

	int* type = (int*) malloc(dataLength*sizeof(int));
		
	float* spaceFillingValue = oriData; //
	
	DynamicIntArray *exactLeadNumArray;
	new_DIA(&exactLeadNumArray, DynArrayInitLen);
	
	DynamicByteArray *exactMidByteArray;
	new_DBA(&exactMidByteArray, DynArrayInitLen);
	
	DynamicIntArray *resiBitArray;
	new_DIA(&resiBitArray, DynArrayInitLen);
	
	unsigned char preDataBytes[4];
	intToBytes_bigEndian(preDataBytes, 0);
	
	int reqBytesLength = reqLength/8;
	int resiBitsLength = reqLength%8;

	FloatValueCompressElement *vce = (FloatValueCompressElement*)malloc(sizeof(FloatValueCompressElement));
	LossyCompressionElement *lce = (LossyCompressionElement*)malloc(sizeof(LossyCompressionElement));
				
	//add the first data	
	type[0] = 0;
	compressSingleFloatValue(vce, spaceFillingValue[0], realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
	updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
	memcpy(preDataBytes,vce->curBytes,4);
	addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
	decData[0] = vce->data;
		
	//add the second data
	type[1] = 0;
	compressSingleFloatValue(vce, spaceFillingValue[1], realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
	updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
	memcpy(preDataBytes,vce->curBytes,4);
	addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
	decData[1] = vce->data;	
	
	int state = 0;
	double checkRadius = 0;
	float curData = 0;
	float pred = 0;
	float predAbsErr = 0;
	checkRadius = (exe_params->intvCapacity-1)*realPrecision;
	double interval = 2*realPrecision;
	int j = 0;
	while (sz_tsc->bit_array[j] == '1'){j++;}
	j++;
	while (sz_tsc->bit_array[j] == '1'){j++;}
	j++;


	int TmArrID = sz_tsc->currentStep - 1;
	float delta_t = delta_t_opt[TmArrID];

	//for(i=2;i<dataLength && j < 418605;i++, j++)//sihuan update:
	for(i=2;i<dataLength;i++, j++)
	{
		while(sz_tsc->bit_array[j] == '1'){j++;}
		curData = spaceFillingValue[i];
		//pred = preStepData[i];
		//pred = preStepData[j];
		pred = preStepData[j] + preStepData_vol[j]*delta_t;
		predAbsErr = fabs(curData - pred);	
		if(predAbsErr<=checkRadius)
		{
			state = (predAbsErr/realPrecision+1)/2;
			if(curData>=pred)
			{
				type[i] = exe_params->intvRadius+state;
				pred = pred + state*interval;
			}
			else //curData<pred
			{
				type[i] = exe_params->intvRadius-state;
				pred = pred - state*interval;
			}
				
			//double-check the prediction error in case of machine-epsilon impact	
			if(fabs(curData-pred)>realPrecision)
			{	
				type[i] = 0;				
				compressSingleFloatValue(vce, curData, realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
				updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
				memcpy(preDataBytes,vce->curBytes,4);
				addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);		
				decData[i] = vce->data;
			}
			else
			{
				decData[i] = pred;
			}
			
			continue;
		}
		
		//unpredictable data processing		
		type[i] = 0;		
		compressSingleFloatValue(vce, curData, realPrecision, medianValue, reqLength, reqBytesLength, resiBitsLength);
		updateLossyCompElement_Float(vce->curBytes, preDataBytes, reqBytesLength, resiBitsLength, lce);
		memcpy(preDataBytes,vce->curBytes,4);
		addExactData(exactMidByteArray, exactLeadNumArray, resiBitArray, lce);
		decData[i] = vce->data;
	}//end of for

	printf("after doing compression, the loop iterations, data length are: %zu, %zu\n", i, dataLength);
		
	size_t exactDataNum = exactLeadNumArray->size;
	
	TightDataPointStorageF* tdps;
			
	new_TightDataPointStorageF(&tdps, dataLength, exactDataNum, 
			type, exactMidByteArray->array, exactMidByteArray->size,  
			exactLeadNumArray->array,  
			resiBitArray->array, resiBitArray->size, 
			resiBitsLength,
			realPrecision, medianValue, (char)reqLength, quantization_intervals, NULL, 0, 0);

	//free memory
	free_DIA(exactLeadNumArray);
	free_DIA(resiBitArray);
	free(type);	
	free(vce);
	free(lce);	
	free(exactMidByteArray); //exactMidByteArray->array has been released in free_TightDataPointStorageF(tdps);
		
	memcpy(preStepData, decData, dataLength*sizeof(float)); //update the data
	free(decData);
	
	return tdps;
}


