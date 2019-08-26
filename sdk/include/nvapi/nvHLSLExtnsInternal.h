 /************************************************************************************************************************************\
|*                                                                                                                                    *|
|*     Copyright © 2012 NVIDIA Corporation.  All rights reserved.                                                                     *|
|*                                                                                                                                    *|
|*  NOTICE TO USER:                                                                                                                   *|
|*                                                                                                                                    *|
|*  This software is subject to NVIDIA ownership rights under U.S. and international Copyright laws.                                  *|
|*                                                                                                                                    *|
|*  This software and the information contained herein are PROPRIETARY and CONFIDENTIAL to NVIDIA                                     *|
|*  and are being provided solely under the terms and conditions of an NVIDIA software license agreement.                             *|
|*  Otherwise, you have no rights to use or access this software in any manner.                                                       *|
|*                                                                                                                                    *|
|*  If not covered by the applicable NVIDIA software license agreement:                                                               *|
|*  NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOFTWARE FOR ANY PURPOSE.                                            *|
|*  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.                                                           *|
|*  NVIDIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,                                                                     *|
|*  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.                       *|
|*  IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,                               *|
|*  OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,  WHETHER IN AN ACTION OF CONTRACT,                         *|
|*  NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.            *|
|*                                                                                                                                    *|
|*  U.S. Government End Users.                                                                                                        *|
|*  This software is a "commercial item" as that term is defined at 48 C.F.R. 2.101 (OCT 1995),                                       *|
|*  consisting  of "commercial computer  software"  and "commercial computer software documentation"                                  *|
|*  as such terms are  used in 48 C.F.R. 12.212 (SEPT 1995) and is provided to the U.S. Government only as a commercial end item.     *|
|*  Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through 227.7202-4 (JUNE 1995),                                          *|
|*  all U.S. Government End Users acquire the software with only those rights set forth herein.                                       *|
|*                                                                                                                                    *|
|*  Any use of this software in individual and commercial software must include,                                                      *|
|*  in the user documentation and internal comments to the code,                                                                      *|
|*  the above Disclaimer (as applicable) and U.S. Government End Users Notice.                                                        *|
|*                                                                                                                                    *|
 \************************************************************************************************************************************/

////////////////////////// NVIDIA SHADER EXTENSIONS /////////////////
// internal functions
// Functions in this file are not expected to be called by apps directly

#include "nvShaderExtnEnums.h"

struct NvShaderExtnStruct
{
    uint   opcode;      // opcode
    uint   rid;         // resource ID
    uint   sid;         // sampler ID

    float4 src0f;       // float source operand  0
    float4 src1f;       // float source operand  0
    float4 src2f;       // float source operand  0
    float4 dst0f;       // float destination operand 

    uint4  src0u;       // uint source operand  0
    uint4  src1u;       // uint source operand  0
    uint4  src2u;       // uint source operand  0
    uint4  dst0u;       // uint destination operand

    uint   markUavRef;  // the next store to UAV is fake and is used only to identify the uav slot
    float  padding[28]; // struct size: 256 bytes
};

// RW structured buffer for Nvidia shader extensions

// Application needs to define NV_SHADER_EXTN_SLOT as a unused slot, which should be 
// set using NvAPI_D3D11_SetNvShaderExtnSlot() call before creating the first shader that
// uses nvidia shader extensions. E.g before including this file in shader define it as:
// #define NV_SHADER_EXTN_SLOT u7

// Note that other operations to this UAV will be ignored so application
// should bind a null resource
RWStructuredBuffer<NvShaderExtnStruct> g_NvidiaExt : register( NV_SHADER_EXTN_SLOT );


//----------------------------------------------------------------------------//
// the exposed SHFL instructions accept a mask parameter in src2
// To compute lane mask from width of segment:
// minLaneID : currentLaneId & src2[12:8]
// maxLaneID : minLaneId | (src2[4:0] & ~src2[12:8])
// where [minLaneId, maxLaneId] defines the segment where currentLaneId belongs
// we always set src2[4:0] to 11111 (0x1F), and set src2[12:8] as (32 - width)
int __NvGetShflMaskFromWidth(uint width)
{
    return ((NV_WARP_SIZE - width) << 8) | 0x1F;
}

