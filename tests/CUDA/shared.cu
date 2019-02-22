__device__ void mybarrier() {
  __syncthreads();
}

__global__ void reduce(int *g_idata, int *g_odata) {
  extern __shared__ int sdata[];
  // each thread loads one element from global to shared mem
  unsigned int tid = threadIdx.x;
  unsigned int i = blockIdx.x*blockDim.x + threadIdx.x;
  sdata[tid] = g_idata[i];

  __shared__ float toto;
  toto = 0.0;
  __syncthreads(); // OK

  if (tid == 0) {
    toto = 1.0;
    __syncthreads(); // error
  }


  if (toto)
    __syncthreads(); // error

  mybarrier();

  // Now toto has the same value for all threads in the workgroup.
  if (toto)
    __syncthreads(); // OK

  // do reduction in shared mem
  for(unsigned int s=1; s < blockDim.x; s *= 2) {
    if (tid % (2*s) == 0) {
      sdata[tid] += sdata[tid + s];
    }
    __syncthreads(); // OK
  }
  // write result for this block to global mem
  if (tid == 0) g_odata[blockIdx.x] = sdata[0];

  if (tid == 0)
    toto = 1.0;

  __syncthreads();

  if (toto > 10)
    __syncthreads(); // OK
}