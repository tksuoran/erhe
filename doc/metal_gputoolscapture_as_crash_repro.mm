#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <cstdio>
int main()
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        printf("device: %s raytracing: %d\n", device.name.UTF8String, device.supportsRaytracing);
        id<MTLCommandQueue> queue = [device newCommandQueue];

        float vertices[9] = { 0,0,0,  1,0,0,  0,1,0 };
        uint32_t indices[3] = { 0,1,2 };
        id<MTLBuffer> vb = [device newBufferWithBytes:vertices length:sizeof(vertices) options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib = [device newBufferWithBytes:indices  length:sizeof(indices)  options:MTLResourceStorageModeShared];

        MTLAccelerationStructureTriangleGeometryDescriptor* geom = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
        geom.vertexBuffer = vb; geom.vertexStride = 12; geom.vertexFormat = MTLAttributeFormatFloat3;
        geom.indexBuffer = ib; geom.indexType = MTLIndexTypeUInt32; geom.triangleCount = 1;
        MTLPrimitiveAccelerationStructureDescriptor* desc = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
        desc.geometryDescriptors = @[geom];

        MTLAccelerationStructureSizes sizes = [device accelerationStructureSizesWithDescriptor:desc];
        id<MTLAccelerationStructure> as = [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
        id<MTLBuffer> scratch = [device newBufferWithLength:sizes.buildScratchBufferSize options:MTLResourceStorageModePrivate];

        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLAccelerationStructureCommandEncoder> enc = [cb accelerationStructureCommandEncoder];
        printf("encoder class: %s, cb class: %s\n", object_getClassName(enc), object_getClassName(cb));
        [enc buildAccelerationStructure:as descriptor:desc scratchBuffer:scratch scratchBufferOffset:0];
        [enc endEncoding];
        printf("endEncoding survived\n");
        [cb commit];
        [cb waitUntilCompleted];
        printf("build completed, status %ld\n", (long)cb.status);
    }
    return 0;
}
