#ifdef _OPENCL
#include "OpenCLTools.h"

#include <iostream>

#include "shadowdetection/opencv/OpenCV2Tools.h"
#include "shadowdetection/opencv/OpenCVTools.h"
#include "typedefs.h"
#include "shadowdetection/util/Config.h"
#include "shadowdetection/util/raii/RAIIS.h"

#define KERNEL_FILE "image_hci_convert_kernel"
#define KERNEL_PATH "src/shadowdetection/opencl/kernels/"

namespace shadowdetection {
    namespace opencl {

        using namespace std;
        using namespace shadowdetection::opencv2;
        using namespace shadowdetection::opencv;
        using namespace cv;
        using namespace shadowdetection::util;
        using namespace shadowdetection::util::raii;

        size_t shrRoundUp(size_t localSize, size_t allSize) {
            if (allSize % localSize == 0) {
                return allSize;
            }
            int coef = allSize / localSize;
            return ((coef + 1) * localSize);
        }

        void OpenclTools::err_check(int err, string err_code) throw (SDException&) {
            if (err != CL_SUCCESS) {
                cout << "Error: " << err_code << "(" << err << ")" << endl;
                if (err == CL_BUILD_PROGRAM_FAILURE) {
                    // Determine the size of the log
                    size_t log_size;
                    clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
                    // Allocate memory for the log
                    char *log = (char *) malloc(log_size);
                    // Get the log
                    clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
                    // Print the log
                    cout << log << endl;
                }
                SDException exc(SHADOW_OTHER, err_code);
                throw exc;
            }
        }
        
        void OpenclTools::initVars(){
//            platform = 0;
//            for (int i = 0; i < MAX_DEVICES; i++) {
//                devices[i] = 0;
//            }
            command_queue = 0;
            program = 0;
            for (int i = 0; i < KERNEL_COUNT; i++) {
                kernel[i] = 0;
            }
            context = 0;
            initWorkVars();
        }
        
        void OpenclTools::initWorkVars(){
            input = 0;
            output1 = 0;
            output2 = 0;
            output3 = 0;
            ratios1 = 0;
            ratios2 = 0;
        }
        
        OpenclTools::OpenclTools() {
            initVars();
        }

        void OpenclTools::cleanWorkPart() {
            if (input)
                clReleaseMemObject(input);
            if (output1)
                clReleaseMemObject(output1);
            if (output2)
                clReleaseMemObject(output2);
            if (output3)
                clReleaseMemObject(output3);            

            if (ratios1)
                delete[] ratios1;
            if (ratios2)
                delete[] ratios2;
            
            initWorkVars();
        }

        void OpenclTools::cleanUp() {
            for (int i = 0; i < KERNEL_COUNT; i++) {
                if (kernel[i] != 0)
                    clReleaseKernel(kernel[i]);
            }
            if (program)
                clReleaseProgram(program);
            if (command_queue)
                clReleaseCommandQueue(command_queue);
            if (context)
                clReleaseContext(context);
            cleanWorkPart();
            
            initVars();
        }

        OpenclTools::~OpenclTools() {
            cleanUp();
        }
        
        void OpenclTools::loadKernelFile(string& kernelFileName){
            string usePrecompiledStr = Config::getInstancePtr()->getPropertyValue("settings.openCL.UsePrecompiledKernels");
            bool usePrecompiled = usePrecompiledStr.compare("true") == 0;
            if (usePrecompiled){
                bool succ = loadKernelFileFromBinary(kernelFileName);
                if (succ){
                    return;
                }
            }
            loadKernelFileFromSource(kernelFileName);
            if (usePrecompiled){
                const char* fp = saveKernelBinary(kernelFileName);
                if (fp != 0){
                    remove(fp);
                    delete[] fp;
                }
            }
        }
        
        void OpenclTools::loadKernelFileFromSource(string& kernelFileName){
            fstream kernelFile;
            string file = KERNEL_PATH + kernelFileName + ".cl";
            kernelFile.open(file.c_str(), ifstream::in);
            FileRaii fRaii(&kernelFile);
            if (kernelFile.is_open()) {
                char* buffer = 0;
                buffer = new(nothrow) char[MAX_SRC_SIZE];
                if (buffer) {
                    kernelFile.read(buffer, MAX_SRC_SIZE);
                    if (kernelFile.eof()) {
                        size_t readBytes = kernelFile.gcount();
                        program = clCreateProgramWithSource(context, 1, (const char **) &buffer, &readBytes, &err);
                        err_check(err, "clCreateProgramWithSource");

                        err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
                        err_check(err, "clBuildProgram");
                    }
                    delete[] buffer;
                } else {                    
                    SDException exc(SHADOW_NO_MEM, "Init Kernel");
                    throw exc;
                }                
            } else {
                SDException exc(SHADOW_READ_UNABLE, "Init Kernel");
                throw exc;
            }
        }
        
        bool OpenclTools::loadKernelFileFromBinary(string& kernelFileName){
            fstream kernelFile;
            char deviceName[256];
            err = clGetDeviceInfo(device, CL_DEVICE_NAME, 256, deviceName, 0);
            try{
                err_check(err, "Get device name, load binary kernel");
            }
            catch (SDException& e){
                cout << e.what() << endl;
                return false;
            }
            string file = deviceName;
            file += "_" + kernelFileName + ".ptx";
            kernelFile.open(file.c_str(), ifstream::in | ifstream::binary);
            FileRaii fRaii(&kernelFile);
            if (kernelFile.is_open()) {
                char* buffer = 0;
                buffer = new char[MAX_SRC_SIZE];
                if (buffer) {
                    kernelFile.read(buffer, MAX_SRC_SIZE);
                    if (kernelFile.eof()) {
                        size_t readBytes = kernelFile.gcount();
                        program = clCreateProgramWithBinary(context, 1, &device, &readBytes, (const unsigned char**)&buffer, 0, &err);
                        try{
                            err_check(err, "clCreateProgramWithBinary");
                        }
                        catch (SDException& e){
                            cout << e.what() << endl;
                            return false;
                        }
                        err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
                        try{
                            err_check(err, "clBuildProgram");
                        }
                        catch (SDException& e){
                            cout << e.what() << endl;
                            return false;
                        }
                    }
                    delete[] buffer;
                } else {                    
                    return false;
                }                
            } else {
                return false;
            }
            return true;
        }
        
        char* getCStrCopy(string str){
            char* ret = new char[str.size() + 1];
            ret[str.size()] = '\0';
            strcpy(ret, str.c_str());
            return ret;
        }
        
        const char* OpenclTools::saveKernelBinary(string& kernelFileName){
            cl_device_type type;
            clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(cl_device_type), &type, 0);
            char deviceName[256];
            err = clGetDeviceInfo(device, CL_DEVICE_NAME, 256, deviceName, 0);
            try{
                err_check(err, "Get device name, save binary kernel");
            }
            catch (SDException& e){
                cout << e.what() << endl;
                return 0;
            }
            string file = deviceName;
            file += "_" + kernelFileName + ".ptx";
            fstream kernel;            
            kernel.open(file.c_str(), ofstream::out | ofstream::binary);
            FileRaii fRaii(&kernel);
            if (kernel.is_open()){                
                cl_uint nb_devices;
                size_t retBytes;
                err = clGetProgramInfo(program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &nb_devices, &retBytes);
                try{
                    err_check(err, "Get num of devices");
                }
                catch (SDException& e){
                    cout << e.what() << endl;
                    return getCStrCopy(file);
                }
                
                size_t* binarySize = 0;
                binarySize = (size_t*)malloc(sizeof(size_t) * nb_devices);
                if (binarySize == 0){
                    SDException exc(SHADOW_NO_MEM, "Get binary sizes");
                    cout << exc.what() << endl;
                    return getCStrCopy(file);
                }
                VectorRaii bsRaii(binarySize);
                err = clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t) * nb_devices, binarySize, 0);
                try{
                    err_check(err, "Get binary size");
                }
                catch (SDException& e){
                    cout << e.what() << endl;
                    return getCStrCopy(file);
                }
                unsigned char**  buffer = 0;
                buffer = (unsigned char**)malloc(sizeof(unsigned char*) * nb_devices);                
                if (buffer != 0){                    
                    for (int i = 0; i < nb_devices; i++){
                        buffer[i] = (unsigned char*)malloc(binarySize[i] * sizeof(unsigned char));                        
                    }
                    MatrixRaii mRaii((void**)buffer, nb_devices);
                    
                    size_t read;
                    err = clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(unsigned char *)*nb_devices, buffer, &read);
                    try{
                        err_check(err, "Get kernel binaries");
                    }
                    catch (SDException& e){
                        cout << e.what() << endl;
                        return getCStrCopy(file);
                    }
                    //because I know that is on one device
                    kernel.write((const char*)buffer[0], binarySize[0]);
                }
                else{
                    SDException exc(SHADOW_NO_MEM, "Save binary kernel");
                    cout << exc.what() << endl;
                    return getCStrCopy(file);
                }                
            }
            else{
                SDException exc(SHADOW_WRITE_UNABLE, "Save binary kernel");
                cout << exc.what() << endl;
                return getCStrCopy(file);
            }
            return 0;
        }
        
        void OpenclTools::init(int platformID, int deviceID, bool listOnly) throw (SDException&) {
            char info[256];
            cl_platform_id platform[MAX_PLATFORMS];
            cl_uint num_platforms;                        
            
            err = clGetPlatformIDs(MAX_PLATFORMS, platform, &num_platforms);
            err_check(err, "clGetPlatformIDs");
            cout << "Found " << num_platforms << " platforms." << endl;                        
            cout << "=============" << endl;
            for (int i = 0; i < num_platforms; i++) {
                cl_device_id devices[MAX_DEVICES];
                cl_uint num_devices;
                err = clGetPlatformInfo(platform[i], CL_PLATFORM_NAME, 256, info, 0);
                err_check(err, "clGetPlatformInfo");
                cout << "Platform name: " << info << endl;
                try {
#ifdef _AMD
                    err = clGetDeviceIDs(platform[i], CL_DEVICE_TYPE_CPU, MAX_DEVICES, devices, &num_devices);
#elif defined _MAC
                    string useGPUStr = Config::getInstancePtr()->getPropertyValue("settings.openCL.mac.useGPU");
                    if (useGPUStr.compare("true") == 0)
                        err = clGetDeviceIDs(platform[i], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &num_devices);
                    else
                        err = clGetDeviceIDs(platform[i], CL_DEVICE_TYPE_CPU, MAX_DEVICES, devices, &num_devices);
#else
                    err = clGetDeviceIDs(platform[i], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &num_devices);
#endif
                    err_check(err, "clGetDeviceIDs");
                    cout << "Found " << num_devices << " devices" << endl;

                    for (int j = 0; j < num_devices; j++) {
                        err = clGetDeviceInfo(devices[j], CL_DEVICE_NAME, 256, info, 0);
                        err_check(err, "clGetDeviceInfo");
                        cout << "Device " << j << " name: " << info << endl;
                    }
                }                
                catch (SDException& exception) {
                    cout << "Platform not supported by this build" << endl;
                    cout << exception.what() << endl;
                }
                cout << "=============" << endl;
            }
            
            if (listOnly)
                return;
            
            if (platformID >= num_platforms){
                SDException exc(SHADOW_NO_OPENCL_PLATFORM, "Init platform");
                throw exc;
            }
            
            cl_device_id devices[MAX_DEVICES];
            cl_uint num_devices;
#ifdef _AMD
                err = clGetDeviceIDs(platform[platformID], CL_DEVICE_TYPE_CPU, MAX_DEVICES, devices, &num_devices);
#elif defined _MAC
            string useGPUStr = Config::getInstancePtr()->getPropertyValue("settings.openCL.mac.useGPU");
            if (useGPUStr.compare("true") == 0)
                err = clGetDeviceIDs(platform[platformID], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &num_devices);
            else
                err = clGetDeviceIDs(platform[platformID], CL_DEVICE_TYPE_CPU, MAX_DEVICES, devices, &num_devices);
#else
                err = clGetDeviceIDs(platform[platformID], CL_DEVICE_TYPE_GPU, MAX_DEVICES, devices, &num_devices);
#endif
            err_check(err, "clGetDeviceIDs");
            if (deviceID >= num_devices){
                SDException exc(SHADOW_NO_OPENCL_DEVICE, "Init devices");
                throw exc;
            }
            device = devices[deviceID];

            context = clCreateContext(0, 1, &device, NULL, NULL, &err);
            err_check(err, "clCreateContext");

            cl_bool sup;
            size_t rsize;
            clGetDeviceInfo(device, CL_DEVICE_IMAGE_SUPPORT, sizeof (sup), &sup, &rsize);
            if (sup != CL_TRUE) {
                SDException exception(SHADOW_IMAGE_NOT_SUPPORTED_ON_DEVICE, "Check for image support");
                throw exception;
            }

            command_queue = clCreateCommandQueue(context, device, 0, &err);
            err_check(err, "clCreateCommandQueue");
            
            string kernelFile = KERNEL_FILE;
            loadKernelFile(kernelFile);
            
            return;
        }

        void OpenclTools::createKernels() {
            kernel[0] = clCreateKernel(program, "image_hsi_convert1", &err);
            err_check(err, "clCreateKernel1");
            kernel[1] = clCreateKernel(program, "image_hsi_convert2", &err);
            err_check(err, "clCreateKernel2");
            kernel[2] = clCreateKernel(program, "image_simple_tsai", &err);
            err_check(err, "clCreateKernel3");
        }

        void OpenclTools::createWorkGroupSizes() {
            for (int i = 0; i < KERNEL_COUNT; i++) {
                err = clGetKernelWorkGroupInfo(kernel[i], device, CL_KERNEL_WORK_GROUP_SIZE, sizeof (workGroupSize[i]), &(workGroupSize[i]), NULL);
                err_check(err, "clGetKernelWorkGroupInfo");
            }
        }

        void OpenclTools::createBuffers(uchar* image, u_int32_t height, u_int32_t width, uchar channels) {
            size_t size = width * height * channels;
            cl_device_type type;
            clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(cl_device_type), &type, 0);
            if (type == CL_DEVICE_TYPE_GPU)
            {
                input = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, size, image, &err);
                err_check(err, "clCreateBuffer1");
                output1 = clCreateBuffer(context, CL_MEM_READ_WRITE, size * sizeof (u_int32_t), 0, &err);
                err_check(err, "clCreateBuffer2");
                output2 = clCreateBuffer(context, CL_MEM_READ_WRITE, size * sizeof (u_int32_t), 0, &err);
                err_check(err, "clCreateBuffer3");
                output3 = clCreateBuffer(context, CL_MEM_WRITE_ONLY, width * height, 0, &err);
                err_check(err, "clCreateBuffer4");
            }
            else if (type == CL_DEVICE_TYPE_CPU){
                int flag = CL_MEM_USE_HOST_PTR;
                input = clCreateBuffer(context, CL_MEM_READ_ONLY | flag, size, image, &err);
                err_check(err, "clCreateBuffer1");            
                output1 = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, size * sizeof (u_int32_t), 0, &err);
                err_check(err, "clCreateBuffer2");
                output2 = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, size * sizeof (u_int32_t), 0, &err);
                err_check(err, "clCreateBuffer3");
                output3 = clCreateBuffer(context, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, width * height, 0, &err);
                err_check(err, "clCreateBuffer4");
            }
            else{
                SDException exc(SHADOW_NOT_SUPPORTED_DEVICE, "Init buffers, currently not supported device");
                throw exc;
            }
        }
        
        void OpenclTools::setKernelArgs1(u_int32_t height, u_int32_t width, uchar channels){
            err = clSetKernelArg(kernel[0], 0, sizeof (cl_mem), &input);
            err_check(err, "clSetKernelArg1");
            err = clSetKernelArg(kernel[0], 1, sizeof (cl_mem), &output1);
            err_check(err, "clSetKernelArg1");
            err = clSetKernelArg(kernel[0], 2, sizeof (u_int32_t), &width);
            err_check(err, "clSetKernelArg1");
            err = clSetKernelArg(kernel[0], 3, sizeof (u_int32_t), &height);
            err_check(err, "clSetKernelArg1");
            err = clSetKernelArg(kernel[0], 4, sizeof (uchar), &channels);
            err_check(err, "clSetKernelArg1");

            err = clSetKernelArg(kernel[1], 0, sizeof (cl_mem), &input);
            err_check(err, "clSetKernelArg2");
            err = clSetKernelArg(kernel[1], 1, sizeof (cl_mem), &output2);
            err_check(err, "clSetKernelArg2");
            err = clSetKernelArg(kernel[1], 2, sizeof (u_int32_t), &width);
            err_check(err, "clSetKernelArg2");
            err = clSetKernelArg(kernel[1], 3, sizeof (u_int32_t), &height);
            err_check(err, "clSetKernelArg2");
            err = clSetKernelArg(kernel[1], 4, sizeof (uchar), &channels);
            err_check(err, "clSetKernelArg2");
        }
        
        void OpenclTools::setKernelArgs2(u_int32_t height, u_int32_t width, unsigned char channels){
            err = clSetKernelArg(kernel[2], 0, sizeof (cl_mem), &output1);
            err_check(err, "clSetKernelArg2");
            err = clSetKernelArg(kernel[2], 1, sizeof (cl_mem), &output3);
            err_check(err, "clSetKernelArg2");
            err = clSetKernelArg(kernel[2], 2, sizeof (u_int32_t), &width);
            err_check(err, "clSetKernelArg2");
            err = clSetKernelArg(kernel[2], 3, sizeof (u_int32_t), &height);
            err_check(err, "clSetKernelArg2");
            err = clSetKernelArg(kernel[2], 4, sizeof (uchar), &channels);
            err_check(err, "clSetKernelArg2");
        }
        
        void OpenclTools::setKernelArgs3(u_int32_t height, u_int32_t width, unsigned char channels){
            err = clSetKernelArg(kernel[2], 0, sizeof (cl_mem), &output2);
            err_check(err, "clSetKernelArg3");
            err = clSetKernelArg(kernel[2], 1, sizeof (cl_mem), &output3);
            err_check(err, "clSetKernelArg3");
            err = clSetKernelArg(kernel[2], 2, sizeof (u_int32_t), &width);
            err_check(err, "clSetKernelArg3");
            err = clSetKernelArg(kernel[2], 3, sizeof (u_int32_t), &height);
            err_check(err, "clSetKernelArg3");
            err = clSetKernelArg(kernel[2], 4, sizeof (uchar), &channels);
            err_check(err, "clSetKernelArg3");
        }
        
        Mat* OpenclTools::processRGBImage(uchar* image, u_int32_t width, u_int32_t height, uchar channels) throw (SDException&) {
            if (image == 0) {
                return 0;
            }

            createKernels();
            createWorkGroupSizes();
            createBuffers(image, height, width, channels);            
            
            setKernelArgs1(height, width, channels);            
            size_t local_ws = workGroupSize[0];
            size_t global_ws = shrRoundUp(local_ws, width * height);
            err = clEnqueueNDRangeKernel(command_queue, kernel[0], 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
            err_check(err, "clEnqueueNDRangeKernel1");
            clFlush(command_queue);
            clFinish(command_queue);
            
            setKernelArgs2(height, width, channels);
            local_ws = workGroupSize[2];
            global_ws = shrRoundUp(local_ws, width * height);
            err = clEnqueueNDRangeKernel(command_queue, kernel[2], 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
            err_check(err, "clEnqueueNDRangeKernel3");
            clReleaseMemObject(output1);
            output1 = 0;
            ratios1 = 0;
            ratios1 = new(nothrow) uchar[width * height];
            if (ratios1 == 0) {
                SDException exc(SHADOW_NO_MEM, "Calculate ratios1");
                throw exc;
            }
            err = clEnqueueReadBuffer(command_queue, output3, CL_TRUE, 0, width * height, ratios1, 0, NULL, NULL);
            err_check(err, "clEnqueueReadBuffer1");
            clFlush(command_queue);
            clFinish(command_queue);                       
            
            Mat* ratiosImage1 = OpenCV2Tools::get8bitImage(ratios1, height, width);            
            Mat* binarized1 = OpenCV2Tools::binarize(ratiosImage1);            
            delete[] ratios1;
            ratios1 = 0;
            delete ratiosImage1;

            local_ws = workGroupSize[1];
            global_ws = shrRoundUp(local_ws, width * height);
            err = clEnqueueNDRangeKernel(command_queue, kernel[1], 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
            err_check(err, "clEnqueueNDRangeKernel2");
            clReleaseMemObject(input);
            input = 0;
            clFlush(command_queue);
            clFinish(command_queue);
            
            setKernelArgs3(height, width, channels);
            local_ws = workGroupSize[2];
            global_ws = shrRoundUp(local_ws, width * height);
            err = clEnqueueNDRangeKernel(command_queue, kernel[2], 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
            err_check(err, "clEnqueueNDRangeKernel3");
            clReleaseMemObject(output2);
            output2 = 0;
            ratios2 = 0;
            ratios2 = new(nothrow) uchar[width * height];
            if (ratios2 == 0) {
                SDException exc(SHADOW_NO_MEM, "Calculate ratios2");
                throw exc;
            }
            err = clEnqueueReadBuffer(command_queue, output3, CL_TRUE, 0, width * height, ratios2, 0, NULL, NULL);
            err_check(err, "clEnqueueReadBuffer2");
            clFlush(command_queue);
            clFinish(command_queue);

            clReleaseMemObject(output3);
            output3 = 0;
            clFlush(command_queue);
            clFinish(command_queue);                        
            
            Mat* ratiosImage2 = OpenCV2Tools::get8bitImage(ratios2, height, width);            
            Mat* binarized2 = OpenCV2Tools::binarize(ratiosImage2);            
            delete[] ratios2;
            ratios2 = 0;
            delete ratiosImage2;
            
            Mat* processedImageMat = OpenCV2Tools::joinTwoOcl(*binarized1, *binarized2);
            delete binarized1;
            delete binarized2;
            return processedImageMat;             
        }
        
        Mat* OpenclTools::processRGBImageOld(uchar* image, u_int32_t width, u_int32_t height, uchar channels) throw (SDException&) {
            if (image == 0) {
                return 0;
            }

            createKernels();
            createWorkGroupSizes();
            createBuffers(image, height, width, channels);
            setKernelArgs1(height, width, channels);

            size_t local_ws = workGroupSize[0];
            size_t global_ws = shrRoundUp(local_ws, width * height);
            err = clEnqueueNDRangeKernel(command_queue, kernel[0], 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
            err_check(err, "clEnqueueNDRangeKernel1");
            clFlush(command_queue);
            clFinish(command_queue);

            setKernelArgs2(height, width, channels);
            local_ws = workGroupSize[2];
            global_ws = shrRoundUp(local_ws, width * height);
            err = clEnqueueNDRangeKernel(command_queue, kernel[2], 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
            err_check(err, "clEnqueueNDRangeKernel3");
            clReleaseMemObject(output1);
            output1 = 0;
            ratios1 = 0;
            ratios1 = new(nothrow) uchar[width * height];
            if (ratios1 == 0) {
                SDException exc(SHADOW_NO_MEM, "Calculate ratios1");
                throw exc;
            }
            err = clEnqueueReadBuffer(command_queue, output3, CL_TRUE, 0, width * height, ratios1, 0, NULL, NULL);
            err_check(err, "clEnqueueReadBuffer1");
            clFlush(command_queue);
            clFinish(command_queue);                       
            
            IplImage* ratiosImage1 = OpenCvTools::get8bitImage(ratios1, height, width);
            IplImage* binarized1 = OpenCvTools::binarize(ratiosImage1);
            delete[] ratios1;
            ratios1 = 0;
            cvReleaseImage(&ratiosImage1);

            local_ws = workGroupSize[1];
            global_ws = shrRoundUp(local_ws, width * height);
            err = clEnqueueNDRangeKernel(command_queue, kernel[1], 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
            err_check(err, "clEnqueueNDRangeKernel2");
            clReleaseMemObject(input);
            input = 0;
            clFlush(command_queue);
            clFinish(command_queue);

            setKernelArgs3(height, width, channels);
            local_ws = workGroupSize[2];
            global_ws = shrRoundUp(local_ws, width * height);
            err = clEnqueueNDRangeKernel(command_queue, kernel[2], 1, NULL, &global_ws, &local_ws, 0, NULL, NULL);
            err_check(err, "clEnqueueNDRangeKernel3");
            clReleaseMemObject(output2);
            output2 = 0;
            ratios2 = 0;
            ratios2 = new(nothrow) uchar[width * height];
            if (ratios2 == 0) {
                SDException exc(SHADOW_NO_MEM, "Calculate ratios2");
                throw exc;
            }
            err = clEnqueueReadBuffer(command_queue, output3, CL_TRUE, 0, width * height, ratios2, 0, NULL, NULL);
            err_check(err, "clEnqueueReadBuffer2");
            clFlush(command_queue);
            clFinish(command_queue);

            clReleaseMemObject(output3);
            output3 = 0;
            clFlush(command_queue);
            clFinish(command_queue);                        
            
            IplImage* ratiosImage2 = OpenCvTools::get8bitImage(ratios2, height, width);
            delete[] ratios2;
            ratios2 = 0;
            IplImage* binarized2 = OpenCvTools::binarize(ratiosImage2);
            cvReleaseImage(&ratiosImage2);

            Mat b1 = cvarrToMat(binarized1, false, false);
            Mat b2 = cvarrToMat(binarized2, false, false);
            Mat* processedImageMat = OpenCV2Tools::joinTwoOcl(b1, b2);            
            cvReleaseImage(&binarized1);
            cvReleaseImage(&binarized2);
            return processedImageMat;             
        }

    }
}

#endif