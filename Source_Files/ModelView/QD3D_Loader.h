/*
	QuickDraw 3D / Quesa Object Loader
	
	By Loren Petrich, September 1, 2001
*/
#ifndef QD3D_LOADER
#define QD3D_LOADER

#include <stdio.h>
#include "Model3D.h"
#include "FileHandler.h"

bool LoadModel_QD3D(FileSpecifier& Spec, Model3D& Model);

// Where to emit status messages
void SetDebugOutput_QD3D(FILE *DebugOutput);

// Set the QD3D tesselation factor:
// The first argument means
//    if true, tesselation length = length in world-geometry units
//    if false, tesselation length = constant subdivision factor
// The second argument is that length
void SetTesselation_QD3D(bool IsWorldLength, float TessLength);

// Set to defaults
void SetDefaultTesselation_QD3D();

#endif
