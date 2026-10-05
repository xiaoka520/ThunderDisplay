// Permission-free native swatch check: sRGB RGB -> NV12/P010 -> renderer matrix.
// Tests actual VideoToolbox conversion against the source color values.
import CoreVideo
import VideoToolbox
import CoreGraphics
import Foundation

func require(_ condition: @autoclosure () -> Bool, _ message: String) {
    guard condition() else { fputs("Color check failed: \(message)\n", stderr); exit(1) }
}
func check(_ status: OSStatus, _ stage: String) { require(status == noErr, "\(stage): \(status)") }
let swatches: [(Double,Double,Double)] = [
    (0,0,0),(1,1,1),(0.0625,0.0625,0.0625),(0.125,0.125,0.125),
    (0.25,0.25,0.25),(0.5,0.5,0.5),(0.75,0.75,0.75),
    (1,0,0),(0,1,0),(0,0,1),(1,1,0),(1,0,1),(0,1,1),
    (0.8,0.3,0.15),(0.1,0.55,0.9),(0.9,0.2,0.7)
]
let width=swatches.count*32, height=32
for tenBit in [false,true] {
    var source: CVPixelBuffer?, destination: CVPixelBuffer?, transfer: VTPixelTransferSession?
    let attributes=[kCVPixelBufferIOSurfacePropertiesKey: [:]] as CFDictionary
    check(CVPixelBufferCreate(nil,width,height,tenBit ? kCVPixelFormatType_ARGB2101010LEPacked : kCVPixelFormatType_32BGRA,attributes,&source),"RGB allocation")
    check(CVPixelBufferCreate(nil,width,height,tenBit ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,attributes,&destination),"YUV allocation")
    check(VTPixelTransferSessionCreate(allocator:nil,pixelTransferSessionOut:&transfer),"Transfer session")
    CVBufferSetAttachment(source!,kCVImageBufferCGColorSpaceKey,CGColorSpace(name:CGColorSpace.sRGB)!, .shouldPropagate)
    CVBufferSetAttachment(source!,kCVImageBufferColorPrimariesKey,kCVImageBufferColorPrimaries_ITU_R_709_2,.shouldPropagate)
    CVBufferSetAttachment(source!,kCVImageBufferTransferFunctionKey,kCVImageBufferTransferFunction_sRGB,.shouldPropagate)
    check(CVPixelBufferLockBaseAddress(source!,[]),"Lock RGB")
    let base=CVPixelBufferGetBaseAddress(source!)!, stride=CVPixelBufferGetBytesPerRow(source!)
    for y in 0..<height { for x in 0..<width {
        let (r,g,b)=swatches[x/32]
        if tenBit {
            let pixel=base.advanced(by:y*stride+x*4).assumingMemoryBound(to:UInt32.self)
            pixel.pointee=0xC0000000 | (UInt32((r*1023).rounded())<<20) | (UInt32((g*1023).rounded())<<10) | UInt32((b*1023).rounded())
        } else {
            let pixel=base.advanced(by:y*stride+x*4).assumingMemoryBound(to:UInt8.self)
            pixel[0]=UInt8((b*255).rounded()); pixel[1]=UInt8((g*255).rounded()); pixel[2]=UInt8((r*255).rounded()); pixel[3]=255
        }
    } }
    check(CVPixelBufferUnlockBaseAddress(source!,[]),"Unlock RGB")
    for (key,value) in [(kVTPixelTransferPropertyKey_DestinationColorPrimaries,kCVImageBufferColorPrimaries_ITU_R_709_2),
        (kVTPixelTransferPropertyKey_DestinationTransferFunction,kCVImageBufferTransferFunction_sRGB),
        (kVTPixelTransferPropertyKey_DestinationYCbCrMatrix,kCVImageBufferYCbCrMatrix_ITU_R_709_2)] {
        check(VTSessionSetProperty(transfer!,key:key,value:value),"Set color conversion")
    }
    check(VTPixelTransferSessionTransferImage(transfer!,from:source!,to:destination!),"Convert swatches")
    check(CVPixelBufferLockBaseAddress(destination!, .readOnly),"Lock YUV")
    let yBase=CVPixelBufferGetBaseAddressOfPlane(destination!,0)!, cBase=CVPixelBufferGetBaseAddressOfPlane(destination!,1)!
    let yStride=CVPixelBufferGetBytesPerRowOfPlane(destination!,0), cStride=CVPixelBufferGetBytesPerRowOfPlane(destination!,1)
    let maxValue=tenBit ? 1023.0 : 255.0
    var maximumError=0.0
    for i in swatches.indices {
        let x=i*32+16, y=16
        var luma: Double, cb: Double, cr: Double
        if tenBit {
            let ys=yBase.advanced(by:y*yStride+x*2).assumingMemoryBound(to:UInt16.self)
            let cs=cBase.advanced(by:(y/2)*cStride+(x/2)*4).assumingMemoryBound(to:UInt16.self)
            luma=(Double(ys.pointee>>6)-64)/876; cb=(Double(cs[0]>>6)-512)/896; cr=(Double(cs[1]>>6)-512)/896
        } else {
            let ys=yBase.advanced(by:y*yStride+x).assumingMemoryBound(to:UInt8.self)
            let cs=cBase.advanced(by:(y/2)*cStride+(x/2)*2).assumingMemoryBound(to:UInt8.self)
            luma=(Double(ys.pointee)-16)/219; cb=(Double(cs[0])-128)/224; cr=(Double(cs[1])-128)/224
        }
        let channels=[luma+1.5748*cr,luma-0.187324273*cb-0.468124273*cr,luma+1.8556*cb]
        let sourceChannels=[swatches[i].0,swatches[i].1,swatches[i].2]
        for channel in 0..<3 {
            let expected=(sourceChannels[channel]*maxValue).rounded()
            let actual=min(1,max(0,channels[channel]))*maxValue
            maximumError=max(maximumError,abs(actual-expected))
        }
        let range=tenBit ? 876.0 : 219.0
        if i==0 { require(abs(luma)*range<=1.01,"Black level moved by more than one YUV code") }
        if i==1 { require(abs(luma-1)*range<=1.01,"White level moved by more than one YUV code") }
    }
    check(CVPixelBufferUnlockBaseAddress(destination!, .readOnly),"Unlock YUV")
    VTPixelTransferSessionInvalidate(transfer!)
    require(maximumError<=2.5,"\(tenBit ? "P010" : "NV12") maximum source-code error \(maximumError)")
    print(String(format:"sRGB %@: 16 gray/color swatches passed, black/white preserved, max code error %.3f",tenBit ? "RGB10 -> P010" : "BGRA8 -> NV12",maximumError))
}
