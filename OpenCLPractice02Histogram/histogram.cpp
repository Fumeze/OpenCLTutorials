#include <cstdlib>
#include <iostream>
#include <string.h>
#include <stdio.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif
#include "ocl_macros.h"
#include "bmp_image.h"
#define USE_HOST_MEMORY
const char* histogram_kernel =
"#define BIN_SIZE            256                                                        \n"
"__kernel                                                                               \n"
"void histogram_kernel(__global const uint* data,                                       \n"
"                  __global uint* binResultR,                                           \n"
"                  __global uint* binResultG,                                           \n"
"                  __global uint* binResultB,                                           \n"
"                    int elements_to_process,                                           \n"
"                    int total_pixels)                                                  \n"
"{                                                                                      \n"
"  __local int sharedArrayR[BIN_SIZE];                                                  \n"
"  __local int sharedArrayG[BIN_SIZE];                                                  \n"
"  __local int sharedArrayB[BIN_SIZE];                                                  \n"
"  __global uchar4 * image_data = data;                                                 \n"
"  size_t localId = get_local_id(0);                                                    \n"
"  size_t globalId = get_global_id(0);                                                  \n"
"  size_t groupId = get_group_id(0);                                                    \n"
"  size_t groupSize = get_local_size(0);                                                \n"
"                                                                                       \n"
"  /* initialize shared array to zero */                                                \n"
"  sharedArrayR[localId] = 0;                                                           \n"
"  sharedArrayG[localId] = 0;                                                           \n"
"  sharedArrayB[localId] = 0;                                                           \n"
"                                                                                       \n"
"  barrier(CLK_LOCAL_MEM_FENCE);                                                        \n"
"  int groupOffset = groupId * groupSize * elements_to_process;                         \n"
"                                                                                       \n"
"  /* For the last work group calculate the number of elements required */              \n"
"  if(groupId == (get_num_groups(0) - 1))                                               \n"
"    elements_to_process = ( (total_pixels - groupOffset) + groupSize - 1) /groupSize;  \n"
"                                                                                       \n"
"  /* calculate thread-histograms */                                                    \n"
"  for(int i = 0; i < elements_to_process; ++i)                                         \n"
"  {                                                                                    \n"
"    int index = groupOffset + i * get_local_size(0) + localId;                         \n"
"    if(index > total_pixels)                                                           \n"
"      break;                                                                           \n"
"    //Coalesced read from global memory                                                \n"
"    uchar4 value = image_data[index];                                                  \n"
"    atomic_inc(&sharedArrayR[value.x]);                                                \n"
"    atomic_inc(&sharedArrayG[value.y]);                                                \n"
"    atomic_inc(&sharedArrayB[value.z]);                                                \n"
"    //printf(\"%d %d %d %d %d\\n\", value.x, value.y, value.z, value.w, index); \n"
"  }                                                                                    \n"
"                                                                                       \n"
"  barrier(CLK_LOCAL_MEM_FENCE);                                                        \n"
"                                                                                       \n"
"  //Coalesced write to global memory                                                   \n"
"  binResultR[groupId * BIN_SIZE + localId] = sharedArrayR[localId];                    \n"
"  binResultG[groupId * BIN_SIZE + localId] = sharedArrayR[localId];                    \n"
"  binResultB[groupId * BIN_SIZE + localId] = sharedArrayR[localId];                    \n"
"  //printf(\"%d %d %d %d %d\\n\", groupId, localId, binResultR[groupId * BIN_SIZE + localId], binResultG[groupId * BIN_SIZE + localId], binResultB[groupId * BIN_SIZE + localId]);  \n"
"}                                                                                      \n";

cl_ulong time()
{
	cl_int status = 0;
	cl_int binSize = 256;
	cl_int groupSize = 256;
	cl_int subHistgCnt;
	cl_device_type dType = CL_DEVICE_TYPE_GPU;
	cl_platform_id platform = NULL;
	cl_device_id   device;
	cl_context     context;
	cl_command_queue commandQueue;
	cl_mem         imageBuffer;
	cl_mem     intermediateHistR, intermediateHistG, intermediateHistB; /*Intermediate Image Histogram buffer*/
	cl_uint* midDeviceBinR, * midDeviceBinG, * midDeviceBinB;
	cl_uint* deviceBinR, * deviceBinG, * deviceBinB;
	//Read a BMP Image
	Image* image;
	std::string filename = "sample_color1.bmp";
	ReadBMPImage(filename, &image);
	if (image == NULL)
	{
		printf("File %s not present...\n", filename.c_str());
		return 0;
	}
	subHistgCnt = (image->width * image->height) / (binSize * groupSize);
	midDeviceBinR = (cl_uint*)malloc(binSize * subHistgCnt * sizeof(cl_uint));
	midDeviceBinG = (cl_uint*)malloc(binSize * subHistgCnt * sizeof(cl_uint));
	midDeviceBinB = (cl_uint*)malloc(binSize * subHistgCnt * sizeof(cl_uint));
	deviceBinR = (cl_uint*)malloc(binSize * sizeof(cl_uint));
	deviceBinG = (cl_uint*)malloc(binSize * sizeof(cl_uint));
	deviceBinB = (cl_uint*)malloc(binSize * sizeof(cl_uint));

	//Setup the OpenCL Platform,
	//Get the first available platform. Use it as the default platform
	status = clGetPlatformIDs(1, &platform, NULL);
	LOG_OCL_ERROR(status, "clGetPlatformIDs Failed.");

	//Get the first available device
	status = clGetDeviceIDs(platform, dType, 1, &device, NULL);
	LOG_OCL_ERROR(status, "clGetDeviceIDs Failed.");

	//Create an execution context for the selected platform and device.
	cl_context_properties cps[3] =
	{
		CL_CONTEXT_PLATFORM,
		(cl_context_properties)platform,
		0
	};
	context = clCreateContextFromType(
		cps,
		dType,
		NULL,
		NULL,
		&status);
	LOG_OCL_ERROR(status, "clCreateContextFromType Failed.");

	// Create command queue
	commandQueue = clCreateCommandQueue(context,
		device,
		CL_QUEUE_PROFILING_ENABLE,
		&status);
	LOG_OCL_ERROR(status, "clCreateCommandQueue Failed.");
#if !defined(USE_HOST_MEMORY)
	//Create OpenCL device input buffer
	imageBuffer = clCreateBuffer(
		context,
		CL_MEM_READ_ONLY,
		sizeof(cl_uint) * image->width * image->height,
		NULL,
		&status);
	LOG_OCL_ERROR(status, "clCreateBuffer Failed while creating the image buffer.");

	//Set input data
	cl_event writeEvt;
	status = clEnqueueWriteBuffer(commandQueue,
		imageBuffer,
		CL_FALSE,
		0,
		image->width * image->height * sizeof(cl_uint),
		image->pixels,
		0,
		NULL,
		&writeEvt);
	LOG_OCL_ERROR(status, "clEnqueueWriteBuffer Failed while writing the image data.");
#else
	//Create OpenCL device input buffer
	imageBuffer = clCreateBuffer(
		context,
		CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
		sizeof(cl_uint) * image->width * image->height,
		image->pixels,
		&status);
	LOG_OCL_ERROR(status, "clCreateBuffer Failed while creating the image buffer.");
#endif
	status = clFinish(commandQueue);
	LOG_OCL_ERROR(status, "clFinish Failed while writing the image data.");

	//Create OpenCL device output buffer
	intermediateHistR = clCreateBuffer(
		context,
		CL_MEM_WRITE_ONLY,
		sizeof(cl_uint) * binSize * subHistgCnt,
		NULL,
		&status);
	LOG_OCL_ERROR(status, "clCreateBuffer Failed.");

	intermediateHistG = clCreateBuffer(
		context,
		CL_MEM_WRITE_ONLY,
		sizeof(cl_uint) * binSize * subHistgCnt,
		NULL,
		&status);
	LOG_OCL_ERROR(status, "clCreateBuffer Failed.");

	intermediateHistB = clCreateBuffer(
		context,
		CL_MEM_WRITE_ONLY,
		sizeof(cl_uint) * binSize * subHistgCnt,
		NULL,
		&status);
	LOG_OCL_ERROR(status, "clCreateBuffer Failed.");

	// Create a program from the kernel source
	cl_program program = clCreateProgramWithSource(context, 1,
		(const char**)& histogram_kernel, NULL, &status);
	LOG_OCL_ERROR(status, "clCreateProgramWithSource Failed.");

	// Build the program
	status = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
	if (status != CL_SUCCESS)
		LOG_OCL_COMPILER_ERROR(program, device);

	int elements_to_process;
	int total_pixels = (image->width * image->height);
	elements_to_process = (total_pixels + subHistgCnt * groupSize - 1) / (subHistgCnt * groupSize);

	// Create the OpenCL kernel
	cl_kernel kernel = clCreateKernel(program, "histogram_kernel", &status);
	LOG_OCL_ERROR(status, "clCreateKernel Failed.");
	// Set the arguments of the kernel
	status = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void*)& imageBuffer);
	status |= clSetKernelArg(kernel, 1, sizeof(cl_mem), (void*)& intermediateHistR);
	status |= clSetKernelArg(kernel, 2, sizeof(cl_mem), (void*)& intermediateHistG);
	status |= clSetKernelArg(kernel, 3, sizeof(cl_mem), (void*)& intermediateHistB);
	status |= clSetKernelArg(kernel, 4, sizeof(int), (void*)& elements_to_process);
	status |= clSetKernelArg(kernel, 5, sizeof(int), (void*)& total_pixels);
	LOG_OCL_ERROR(status, "clSetKernelArg Failed.");
	// Execute the OpenCL kernel on the list
	cl_event ndRangeEvt;
	size_t globalThreads = (image->width * image->height) / (binSize * groupSize) * groupSize;
	size_t localThreads = groupSize;
	status = clEnqueueNDRangeKernel(
		commandQueue,
		kernel,
		1,
		NULL,
		&globalThreads,
		&localThreads,
		0,
		NULL,
		&ndRangeEvt);
	LOG_OCL_ERROR(status, "clEnqueueNDRangeKernel Failed.");

	status = clFinish(commandQueue);
	LOG_OCL_ERROR(status, "clFinish Failed.");

	//Read the histogram back into the host memory.
	memset(deviceBinR, 0, binSize * sizeof(cl_uint));
	memset(deviceBinG, 0, binSize * sizeof(cl_uint));
	memset(deviceBinB, 0, binSize * sizeof(cl_uint));
	cl_event readEvt[3];
	status = clEnqueueReadBuffer(
		commandQueue,
		intermediateHistR,
		CL_FALSE,
		0,
		subHistgCnt * binSize * sizeof(cl_uint),
		midDeviceBinR,
		0,
		NULL,
		&readEvt[0]);
	LOG_OCL_ERROR(status, "clEnqueueReadBuffer of intermediateHistR Failed.");

	status = clEnqueueReadBuffer(
		commandQueue,
		intermediateHistG,
		CL_FALSE,
		0,
		subHistgCnt * binSize * sizeof(cl_uint),
		midDeviceBinG,
		0,
		NULL,
		&readEvt[1]);
	LOG_OCL_ERROR(status, "clEnqueueReadBuffer of intermediateHistG Failed.");

	status = clEnqueueReadBuffer(
		commandQueue,
		intermediateHistB,
		CL_FALSE,
		0,
		subHistgCnt * binSize * sizeof(cl_uint),
		midDeviceBinB,
		0,
		NULL,
		&readEvt[2]);
	LOG_OCL_ERROR(status, "clEnqueueReadBuffer of intermediateHistB Failed.");

	status = clWaitForEvents(3, readEvt);
	//status = clFinish(commandQueue);
	LOG_OCL_ERROR(status, "clWaitForEvents for readEvt.");

	// Calculate final histogram bin
	for (int i = 0; i < subHistgCnt; ++i)
	{
		for (int j = 0; j < binSize; ++j)
		{
			deviceBinR[j] += midDeviceBinR[i * binSize + j];
			deviceBinG[j] += midDeviceBinG[i * binSize + j];
			deviceBinB[j] += midDeviceBinB[i * binSize + j];
		}
	}

	// Validate the histogram operation.
	// The idea behind this is that once a histogram is computed the sum of all the bins should be equal to the number of pixels.
	int totalPixelsR = 0;
	int totalPixelsG = 0;
	int totalPixelsB = 0;
	for (int j = 0; j < binSize; ++j)
	{
		totalPixelsR += deviceBinR[j];
		totalPixelsG += deviceBinG[j];
		totalPixelsB += deviceBinB[j];
	}
	//printf("Total Number of Red Pixels = %d\n", totalPixelsR);
	//printf("Total Number of Green Pixels = %d\n", totalPixelsG);
	//printf("Total Number of Blue Pixels = %d\n", totalPixelsB);

	cl_ulong start = 0, stop = 0;
	status = clWaitForEvents(1, &ndRangeEvt);
	status |= clGetEventProfilingInfo(ndRangeEvt, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, NULL);
	status |= clGetEventProfilingInfo(ndRangeEvt, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &stop, NULL);
	cl_ulong run_time_gpu = stop - start;

	clReleaseCommandQueue(commandQueue);
	clReleaseDevice(device);
	clReleaseKernel(kernel);
	clReleaseProgram(program);
	clReleaseMemObject(imageBuffer);
	clReleaseMemObject(intermediateHistR);
	clReleaseMemObject(intermediateHistG);
	clReleaseMemObject(intermediateHistB);
	clReleaseEvent(ndRangeEvt);
	for (int i = 0; i < 3; i++)
		clReleaseEvent(readEvt[i]);
	clReleaseContext(context);

	ReleaseBMPImage(&image);
	//free all allocated memory
	free(midDeviceBinR);
	free(midDeviceBinG);
	free(midDeviceBinB);
	free(deviceBinR);
	free(deviceBinG);
	free(deviceBinB);
	return run_time_gpu;
}

int main(int argc, char* argv[]) {
	int result = 0;
	int num = 0;
	for (int i = 0; i < 100; i++) {
		cl_ulong x = time();
		num++;
		result += (int)(x - result) / num;
	}
	printf("100times average time : %d", result);
	char c = getchar();
	return result;
}