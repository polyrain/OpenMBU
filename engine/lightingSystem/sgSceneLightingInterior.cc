//-----------------------------------------------
// Synapse Gaming - Lighting System
// Copyright � Synapse Gaming 2003
// Written by John Kabus
//-----------------------------------------------
#include "editor/editTSCtrl.h"
#include "editor/worldEditor.h"
#include "game/shadow.h"
#include "game/vehicles/wheeledVehicle.h"
#include "game/gameConnection.h"
#include "sceneGraph/sceneGraph.h"
#include "terrain/terrRender.h"
#include "game/shapeBase.h"
#include "gui/core/guiCanvas.h"
#include "ts/tsShape.h"
#include "ts/tsShapeInstance.h"
#include "game/staticShape.h"
#include "game/tsStatic.h"
#include "collision/concretePolyList.h"
#include "lightingSystem/sgSceneLighting.h"
#include "lightingSystem/sgLightMap.h"
#include "lightingSystem/sgSceneLightingGlobals.h"
#include "lightingSystem/sgLightingModel.h"


/// adds the ability to bake point lights into interior light maps.
void SceneLighting::InteriorProxy::sgAddLight(LightInfo* light, InteriorInstance* interior)
{
    // need this...
    sgInterior = interior;

    sgLightingModel& model = sgLightingModelManager::sgGetLightingModel(
        light->sgLightingModelName);
    model.sgSetState(light);

    // test for early out...
    if (!model.sgCanIlluminate(interior->getWorldBox()))
    {
        model.sgResetState();
        return;
    }

    model.sgResetState();
    sgLights.push_back(light);

    // on first light build surface list...
    if (sgLights.size() == 1)
    {
        // stats...
        sgStatistics::sgInteriorObjectIncludedCount++;


        // get the global shadow casters...
        sgShadowObjects::sgGetObjects(interior);

        sgSurfaces.clear();
        sgCurrentSurfaceIndex = 0;
        InteriorResource* res = sgInterior->getResource();
        U32 countd = res->getNumDetailLevels();
        for (U32 d = 0; d < countd; d++)
        {
            Interior* detail = res->getDetailLevel(d);
            // do NOT call this here!!!
            //gInteriorLMManager.clearLightmaps(detail->getLMHandle(), sgInterior->getLMHandle());
            bool hasAlarm = detail->hasAlarmState();
            U32 counti = detail->getSurfaceCount();
            U32 offset = sgSurfaces.size();
            sgSurfaces.increment(counti);
            for (U32 i = 0; i < counti; i++)
            {
                sgSurfaceInfo& info = sgSurfaces[i + offset];
                info.sgDetail = detail;
                info.sgHasAlarm = hasAlarm;
                info.sgIndex = i;
                info.sgSurface = &detail->getSurface(i);
            }
        }
    }

    // recalc number of surfaces per pass based on new light count...
    sgSurfacesPerPass = sgSurfaces.size() / sgLights.size();
}

void SceneLighting::InteriorProxy::light(LightInfo* light)
{
    U32 i;
    U32 countthispass = 0;


    // stats...
    sgStatistics::sgInteriorObjectIlluminationCount++;


    for (i = sgCurrentSurfaceIndex; i < sgSurfaces.size(); i++)
    {
        sgSurfaceInfo& info = sgSurfaces[i];
        sgProcessSurface((*info.sgSurface), info.sgIndex, info.sgDetail, info.sgHasAlarm);
        countthispass++;
        sgCurrentSurfaceIndex++;
        if ((countthispass >= sgSurfacesPerPass) && (sgLights.last() != light))
            break;
    }
}

void SceneLighting::InteriorProxy::sgProcessSurface(const Interior::Surface& surface,
    U32 i, Interior* detail, bool hasAlarm)
{
    // points right way?
    PlaneF plane = detail->getPlane(surface.planeIndex);
    if (Interior::planeIsFlipped(surface.planeIndex))
        plane.neg();

    const MatrixF& transform = sgInterior->getTransform();
    const Point3F& scale = sgInterior->getScale();

    //
    PlaneF projPlane;
    mTransformPlane(transform, scale, plane, &projPlane);

    //-----------------------------
    // from interiorProxy:

    //-----------------------------------
    // John Kabus - 7/25/2004
    //
    // Support for interior light map border sizes.
    //
    S32 xlen, ylen, xoff, yoff;
    S32 lmborder = detail->getLightMapBorderSize();
    xlen = surface.mapSizeX + (lmborder * 2);
    ylen = surface.mapSizeY + (lmborder * 2);
    xoff = surface.mapOffsetX - lmborder;
    yoff = surface.mapOffsetY - lmborder;

    // get the generic instance 0 lm...
    GBitmap* lm = gInteriorLMManager.getBitmap(detail->getLMHandle(), 0, detail->getNormalLMapIndex(i));
    AssertFatal((lm), "Why was there no base light map??");


    // very important check!!!
    AssertFatal((
        ((xoff >= 0) && ((xlen + xoff) < lm->getWidth())) &&
        ((yoff >= 0) && ((ylen + yoff) < lm->getHeight()))), "Light map extents exceeded bitmap size!");


    // get the lmagGen...
    const Interior::TexGenPlanes& lmTexGenEQ = detail->getLMTexGenEQ(i);

    const F32* const lGenX = lmTexGenEQ.planeX;
    const F32* const lGenY = lmTexGenEQ.planeY;

    AssertFatal((lGenX[0] * lGenX[1] == 0.f) &&
        (lGenX[0] * lGenX[2] == 0.f) &&
        (lGenX[1] * lGenX[2] == 0.f), "Bad lmTexGen!");
    AssertFatal((lGenY[0] * lGenY[1] == 0.f) &&
        (lGenY[0] * lGenY[2] == 0.f) &&
        (lGenY[1] * lGenY[2] == 0.f), "Bad lmTexGen!");

    // get the axis index for the texgens (could be swapped)
    S32 si;
    S32 ti;
    S32 axis = -1;

    //
    if (lGenX[0] == 0.f && lGenY[0] == 0.f)          // YZ
    {
        axis = 0;
        if (lGenX[1] == 0.f) { // swapped?
            si = 2;
            ti = 1;
        }
        else {
            si = 1;
            ti = 2;
        }
    }
    else if (lGenX[1] == 0.f && lGenY[1] == 0.f)     // XZ
    {
        axis = 1;
        if (lGenX[0] == 0.f) { // swapped?
            si = 2;
            ti = 0;
        }
        else {
            si = 0;
            ti = 2;
        }
    }
    else if (lGenX[2] == 0.f && lGenY[2] == 0.f)     // XY
    {
        axis = 2;
        if (lGenX[0] == 0.f) { // swapped?
            si = 1;
            ti = 0;
        }
        else {
            si = 0;
            ti = 1;
        }
    }

    AssertFatal(!(axis == -1), "SceneLighting::lightInterior: bad TexGen!");

    const F32* pNormal = ((const F32*)plane);

    Point3F start;
    F32* pStart = (F32*)start;

    // get the start point on the lightmap
    F32 lumelScale = 1 / (lGenX[si] * lm->getWidth());
    pStart[si] = (((xoff * lumelScale) / (1 / lGenX[si])) - lGenX[3]) / lGenX[si];
    pStart[ti] = (((yoff * lumelScale) / (1 / lGenY[ti])) - lGenY[3]) / lGenY[ti];
    pStart[axis] = ((pNormal[si] * pStart[si]) + (pNormal[ti] * pStart[ti]) + plane.d) / -pNormal[axis];

    start.convolve(scale);
    transform.mulP(start);

    // get the s/t vecs oriented on the surface
    Point3F sVec;
    Point3F tVec;

    F32* pSVec = ((F32*)sVec);
    F32* pTVec = ((F32*)tVec);

    F32 angle;
    Point3F planeNormal;

    // s
    pSVec[si] = 1.f;
    pSVec[ti] = 0.f;

    planeNormal = plane;
    ((F32*)planeNormal)[ti] = 0.f;
    planeNormal.normalize();

    angle = mAcos(mClampF(((F32*)planeNormal)[axis], -1.f, 1.f));
    pSVec[axis] = (((F32*)planeNormal)[si] < 0.f) ? mTan(angle) : -mTan(angle);

    // t
    pTVec[ti] = 1.f;
    pTVec[si] = 0.f;

    planeNormal = plane;
    ((F32*)planeNormal)[si] = 0.f;
    planeNormal.normalize();

    angle = mAcos(mClampF(((F32*)planeNormal)[axis], -1.f, 1.f));
    pTVec[axis] = (((F32*)planeNormal)[ti] < 0.f) ? mTan(angle) : -mTan(angle);

    // scale the vectors
    sVec *= lumelScale;
    tVec *= lumelScale;

    // project vecs
    transform.mulV(sVec);
    sVec.convolve(scale);

    transform.mulV(tVec);
    tVec.convolve(scale);

    //Point3F & curPos = start;
    Point3F& pos = start;
    Point3F sRun = sVec * xlen;

    // get the lexel area                     
    Point3F cross;
    mCross(sVec, tVec, &cross);
    F32 maxLexelArea = cross.len();

    //-----------------------------

    sgPlanarLightMap* lightmap = new sgPlanarLightMap(xlen, ylen, sgInterior,
        detail, i, projPlane);
    lightmap->sgWorldPosition = pos;

    lightmap->sgLightMapSVector = sVec;
    lightmap->sgLightMapTVector = tVec;
    lightmap->sgSetupLighting();

    for (U32 ii = 0; ii < sgLights.size(); ii++)
    {
        // should we even bother?
        LightInfo* light = sgLights[ii];

        if ((light->mType == LightInfo::Vector) &&
            (!(surface.surfaceFlags & Interior::SurfaceOutsideVisible)))
            continue;

        if (!((light->mType != LightInfo::Vector) &&
            (projPlane.distToPlane(light->mPos) <= 0) &&
            (light->sgLocalAmbientAmount <= 0.0f)))
        {
            lightmap->sgCalculateLighting(light);
        }
    }

    if (lightmap->sgIsDirty())
    {
        GFXTexHandle normHandle = gInteriorLMManager.duplicateBaseLightmap(detail->getLMHandle(), sgInterior->getLMHandle(), detail->getNormalLMapIndex(i));
        GFXTexHandle alarmHandle = 0;
        GFXTexHandle normalmaphandle = gInteriorLMManager.duplicateBaseNormalmap(detail->getLMHandle(), sgInterior->getLMHandle(), detail->getNormalLMapIndex(i));

        GBitmap* normLightmap = normHandle->getBitmap();
        GBitmap* alarmLightmap = 0;
        GBitmap* normalmap = NULL;
        if (normalmaphandle)
            normalmap = normalmaphandle->getBitmap();

        // check if the lightmaps are shared
        if (hasAlarm)
        {
            if (detail->getNormalLMapIndex(i) != detail->getAlarmLMapIndex(i))
            {
                alarmHandle = gInteriorLMManager.duplicateBaseLightmap(detail->getLMHandle(), sgInterior->getLMHandle(), detail->getAlarmLMapIndex(i));
                alarmLightmap = alarmHandle->getBitmap();
            }
        }

        lightmap->sgMergeLighting(normLightmap, normalmap, xoff, yoff);
    }

    delete lightmap;
}

void SceneLighting::addInterior(ShadowVolumeBSP* shadowVolume, InteriorProxy& interior, LightInfo* light, S32 level)
{
    if (light->mType != LightInfo::Vector)
        return;

    ColorF ambient = light->mAmbient;

    bool shadowedTree = true;

    // check if just getting shadow detail
    if (level == SHADOW_DETAIL)
    {
        shadowedTree = false;
        level = interior->mInteriorRes->getNumDetailLevels() - 1;
    }

    Interior* detail = interior->mInteriorRes->getDetailLevel(level);
    bool hasAlarm = detail->hasAlarmState();

    // make sure surfaces do not get processed more than once
    BitVector surfaceProcessed;
    surfaceProcessed.setSize(detail->mSurfaces.size());
    surfaceProcessed.clear();

    bool isoutside = false;
    for (U32 zone = 0; zone < interior->getNumCurrZones(); zone++)
    {
        if (interior->getCurrZone(zone) == 0)
        {
            isoutside = true;
            break;
        }
    }
    if (!isoutside)
        return;

    for (U32 i = 0; i < detail->getNumZones(); i++)
    {
        Interior::Zone& zone = detail->mZones[i];
        for (U32 j = 0; j < zone.surfaceCount; j++)
        {
            U32 surfaceIndex = detail->mZoneSurfaces[zone.surfaceStart + j];

            // dont reprocess a surface
            if (surfaceProcessed.test(surfaceIndex))
                continue;
            surfaceProcessed.set(surfaceIndex);

            Interior::Surface& surface = detail->mSurfaces[surfaceIndex];

            // outside visible?
            if (!(surface.surfaceFlags & Interior::SurfaceOutsideVisible))
                continue;

            // good surface?
            PlaneF plane = detail->getPlane(surface.planeIndex);
            if (Interior::planeIsFlipped(surface.planeIndex))
                plane.neg();

            // project the plane
            PlaneF projPlane;
            mTransformPlane(interior->getTransform(), interior->getScale(), plane, &projPlane);

            // fill with ambient? (need to do here, because surface will not be
            // added to the SVBSP tree)
            F32 dot = mDot(projPlane, light->mDirection);
            if (dot > -gParellelVectorThresh && !(GFX->getPixelShaderVersion() > 0.0))
                continue;

            ShadowVolumeBSP::SVPoly* poly = buildInteriorPoly(shadowVolume, interior, detail,
                surfaceIndex, light, shadowedTree);

            // insert it into the SVBSP tree
            shadowVolume->insertPoly(poly);
        }
    }
}

//------------------------------------------------------------------------------
ShadowVolumeBSP::SVPoly* SceneLighting::buildInteriorPoly(ShadowVolumeBSP* shadowVolumeBSP,
    InteriorProxy& interior, Interior* detail, U32 surfaceIndex, LightInfo* light,
    bool createSurfaceInfo)
{
    // transform and add the points...
    const MatrixF& transform = interior->getTransform();
    const VectorF& scale = interior->getScale();

    const Interior::Surface& surface = detail->mSurfaces[surfaceIndex];

    ShadowVolumeBSP::SVPoly* poly = shadowVolumeBSP->createPoly();

    poly->mWindingCount = surface.windingCount;

    // project these points
    for (U32 j = 0; j < poly->mWindingCount; j++)
    {
        Point3F iPnt = detail->mPoints[detail->mWindings[surface.windingStart + j]].point;
        Point3F tPnt;
        iPnt.convolve(scale);
        transform.mulP(iPnt, &tPnt);
        poly->mWinding[j] = tPnt;
    }

    // convert from fan
    U32 tmpIndices[ShadowVolumeBSP::SVPoly::MaxWinding];
    Point3F fanIndices[ShadowVolumeBSP::SVPoly::MaxWinding];

    tmpIndices[0] = 0;

    U32 idx = 1;
    U32 i;
    for (i = 1; i < poly->mWindingCount; i += 2)
        tmpIndices[idx++] = i;
    for (i = ((poly->mWindingCount - 1) & (~0x1)); i > 0; i -= 2)
        tmpIndices[idx++] = i;

    idx = 0;
    for (i = 0; i < poly->mWindingCount; i++)
        if (surface.fanMask & (1 << i))
            fanIndices[idx++] = poly->mWinding[tmpIndices[i]];

    // set the data
    poly->mWindingCount = idx;
    for (i = 0; i < poly->mWindingCount; i++)
        poly->mWinding[i] = fanIndices[i];

    // flip the plane - shadow volumes face inwards
    PlaneF plane = detail->getPlane(surface.planeIndex);
    if (!Interior::planeIsFlipped(surface.planeIndex))
        plane.neg();

    // transform the plane
    mTransformPlane(transform, scale, plane, &poly->mPlane);
    shadowVolumeBSP->buildPolyVolume(poly, light);

    // do surface info?
    if (createSurfaceInfo)
    {
        ShadowVolumeBSP::SurfaceInfo* surfaceInfo = new ShadowVolumeBSP::SurfaceInfo;
        shadowVolumeBSP->mSurfaces.push_back(surfaceInfo);

        // fill it
        surfaceInfo->mSurfaceIndex = surfaceIndex;
        surfaceInfo->mShadowVolume = shadowVolumeBSP->getShadowVolume(poly->mShadowVolume);

        // POLY and POLY node gets it too
        ShadowVolumeBSP::SVNode* traverse = shadowVolumeBSP->getShadowVolume(poly->mShadowVolume);
        while (traverse->mFront)
        {
            traverse->mSurfaceInfo = surfaceInfo;
            traverse = traverse->mFront;
        }

        // get some info from the poly node
        poly->mSurfaceInfo = traverse->mSurfaceInfo = surfaceInfo;
        surfaceInfo->mPlaneIndex = traverse->mPlaneIndex;
    }

    return(poly);
}


SceneLighting::InteriorProxy::InteriorProxy(SceneObject* obj) :
    Parent(obj)
{
    mBoxShadowBSP = 0;

    sgCurrentSurfaceIndex = 0;
    sgSurfacesPerPass = 0;
}

SceneLighting::InteriorProxy::~InteriorProxy()
{
    delete mBoxShadowBSP;
}

bool SceneLighting::InteriorProxy::loadResources()
{
    InteriorInstance* interior = getObject();
    if (!interior)
        return(false);

    Resource<InteriorResource>& interiorRes = interior->getResource();
    if (!bool(interiorRes))
        return(false);

    return(true);
}

void SceneLighting::InteriorProxy::init()
{
    InteriorInstance* interior = getObject();
    if (!interior)
        return;
}

/// reroutes InteriorProxy::preLight for point light and TSStatic support.
bool SceneLighting::InteriorProxy::preLight(LightInfo* light)
{
    // create shadow volume of the bounding box of this object
    InteriorInstance* interior = getObject();
    if (!interior)
        return(false);

    if (!sgRelightFilter::sgAllowLighting(interior->getWorldBox(), false))
        return false;

    // build light list...
    sgAddLight(light, interior);
    return(true);
}

bool SceneLighting::InteriorProxy::isShadowedBy(InteriorProxy* test)
{
    // add if overlapping world box
    if ((*this)->getWorldBox().isOverlapped((*test)->getWorldBox()))
        return(true);

    // test the box shadow volume
    for (U32 i = 0; i < mLitBoxSurfaces.size(); i++)
    {
        ShadowVolumeBSP::SVPoly* poly = mBoxShadowBSP->copyPoly(mLitBoxSurfaces[i]);
        if (test->mBoxShadowBSP->testPoly(poly))
            return(true);
    }

    return(false);
}

void SceneLighting::InteriorProxy::postLight(bool lastLight)
{
    delete mBoxShadowBSP;
    mBoxShadowBSP = 0;

    InteriorInstance* interior = getObject();
    if (!interior)
        return;
}

//------------------------------------------------------------------------------
U32 SceneLighting::InteriorProxy::getResourceCRC()
{
    InteriorInstance* interior = getObject();
    if (!interior)
        return(0);
    return(interior->getCRC());
}

//------------------------------------------------------------------------------
bool SceneLighting::InteriorProxy::setPersistInfo(PersistInfo::PersistChunk* info)
{

    if (!Parent::setPersistInfo(info))
        return(false);

    PersistInfo::InteriorChunk* chunk = dynamic_cast<PersistInfo::InteriorChunk*>(info);
    AssertFatal(chunk, "SceneLighting::InteriorProxy::setPersistInfo: invalid info chunk!");

    InteriorInstance* interior = getObject();
    if (!interior)
        return(false);

    U32 numDetails = interior->getNumDetailLevels();

    // check the lighting method
    AssertFatal(SceneLighting::smUseVertexLighting == Interior::smUseVertexLighting, "SceneLighting::InteriorProxy::setPersistInfo: invalid vertex lighting state");
    if (SceneLighting::smUseVertexLighting != Interior::smUseVertexLighting)
        return(false);

    // need lightmaps?
    if (!SceneLighting::smUseVertexLighting)
    {
        if (chunk->mDetailLightmapCount.size() != numDetails)
            return(false);

        LM_HANDLE instanceHandle = interior->getLMHandle();
        U32 idx = 0;

        for (U32 i = 0; i < numDetails; i++)
        {
            Interior* detail = interior->getDetailLevel(i);

            LM_HANDLE interiorHandle = detail->getLMHandle();
            Vector<GFXTexHandle>& baseHandles = gInteriorLMManager.getHandles(interiorHandle, 0);

            if (chunk->mDetailLightmapCount[i] > baseHandles.size())
                return(false);

            for (U32 j = 0; j < chunk->mDetailLightmapCount[i]; j++)
            {
                U32 baseIndex = chunk->mDetailLightmapIndices[idx];
                if (baseIndex >= baseHandles.size())
                    return(false);

                AssertFatal(chunk->mLightmaps[idx], "SceneLighting::InteriorProxy::setPersistInfo: bunk bitmap!");
                if (chunk->mLightmaps[idx]->getWidth() != baseHandles[baseIndex]->getWidth() ||
                    chunk->mLightmaps[idx]->getHeight() != baseHandles[baseIndex]->getHeight())
                    return(false);

                GFXTexHandle tHandle = gInteriorLMManager.duplicateBaseLightmap(interiorHandle, instanceHandle, baseIndex);

                // create the diff bitmap
                U8* pDiff = chunk->mLightmaps[idx]->getAddress(0, 0);
                U8* pBase = baseHandles[baseIndex]->getBitmap()->getAddress(0, 0);
                U8* pDest = tHandle->getBitmap()->getAddress(0, 0);

                Point2I extent(tHandle->getWidth(), tHandle->getHeight());
                for (U32 y = 0; y < extent.y; y++)
                {
                    for (U32 x = 0; x < extent.x; x++)
                    {
                        *pDest++ = *pBase++ + *pDiff++;
                        *pDest++ = *pBase++ + *pDiff++;
                        *pDest++ = *pBase++ + *pDiff++;
                    }
                }

                if (chunk->sgNormalLightMaps[idx])
                {
                    GFXTexHandle nlmhandle = gInteriorLMManager.duplicateBaseNormalmap(interiorHandle, instanceHandle, baseIndex);
                    GBitmap* tempnlm = chunk->sgNormalLightMaps[idx];
                    dMemcpy(nlmhandle->getBitmap()->getWritableBits(), tempnlm->getBits(), tempnlm->byteSize);
                }

                idx++;
            }
        }
    }

    return(true);
}

bool SceneLighting::InteriorProxy::getPersistInfo(PersistInfo::PersistChunk* info)
{
    if (!Parent::getPersistInfo(info))
        return(false);

    PersistInfo::InteriorChunk* chunk = dynamic_cast<PersistInfo::InteriorChunk*>(info);
    AssertFatal(chunk, "SceneLighting::InteriorProxy::getPersistInfo: invalid info chunk!");

    InteriorInstance* interior = getObject();
    if (!interior)
        return(false);

    LM_HANDLE instanceHandle = interior->getLMHandle();

    AssertFatal(!chunk->mDetailLightmapCount.size(), "SceneLighting::InteriorProxy::getPersistInfo: invalid array!");
    AssertFatal(!chunk->mDetailLightmapIndices.size(), "SceneLighting::InteriorProxy::getPersistInfo: invalid array!");
    AssertFatal(!chunk->mLightmaps.size(), "SceneLighting::InteriorProxy::getPersistInfo: invalid array!");

    U32 numDetails = interior->getNumDetailLevels();
    U32 i;
    for (i = 0; i < numDetails; i++)
    {
        Interior* detail = interior->getDetailLevel(i);
        LM_HANDLE interiorHandle = detail->getLMHandle();

        Vector<GFXTexHandle>& baseHandles = gInteriorLMManager.getHandles(interiorHandle, 0);
        Vector<GFXTexHandle>& instanceHandles = gInteriorLMManager.getHandles(interiorHandle, instanceHandle);
        Vector<GFXTexHandle>& sgNLMHandles = gInteriorLMManager.getNormalHandles(interiorHandle, instanceHandle);

        U32 litCount = 0;

        // walk all the instance lightmaps and grab diff lighting from them
        for (U32 j = 0; j < instanceHandles.size(); j++)
        {
            if (!instanceHandles[j])
                continue;

            litCount++;
            chunk->mDetailLightmapIndices.push_back(j);

            GBitmap* baseBitmap = baseHandles[j]->getBitmap();
            GBitmap* instanceBitmap = instanceHandles[j]->getBitmap();

            Point2I extent(baseBitmap->getWidth(), baseBitmap->getHeight());

            GBitmap* diffLightmap = new GBitmap(extent.x, extent.y, false);

            U8* pBase = baseBitmap->getAddress(0, 0);
            U8* pInstance = instanceBitmap->getAddress(0, 0);
            U8* pDest = diffLightmap->getAddress(0, 0);

            // fill the diff lightmap
            for (U32 y = 0; y < extent.y; y++)
                for (U32 x = 0; x < extent.x; x++)
                {
                    *pDest++ = *pInstance++ - *pBase++;
                    *pDest++ = *pInstance++ - *pBase++;
                    *pDest++ = *pInstance++ - *pBase++;
                }

            chunk->mLightmaps.push_back(diffLightmap);



            // create a new object...
            if (sgNLMHandles[j])
            {
                GBitmap* nlm = sgNLMHandles[j]->getBitmap();
                GBitmap* tempnlm = new GBitmap(nlm->getWidth(), nlm->getHeight(), false);
                dMemcpy(tempnlm->getWritableBits(), nlm->getBits(), nlm->byteSize);
                chunk->sgNormalLightMaps.push_back(tempnlm);
            }
            else
            {
                chunk->sgNormalLightMaps.push_back(NULL);
            }
        }

        chunk->mDetailLightmapCount.push_back(litCount);
    }

    // process the vertex lighting...
    AssertFatal(!chunk->mDetailVertexCount.size(), "SceneLighting::InteriorProxy::getPersistInfo: invalid chunk info");
    AssertFatal(!chunk->mVertexColorsNormal.size(), "SceneLighting::InteriorProxy::getPersistInfo: invalid chunk info");
    AssertFatal(!chunk->mVertexColorsAlarm.size(), "SceneLighting::InteriorProxy::getPersistInfo: invalid chunk info");

    chunk->mHasAlarmState = interior->getDetailLevel(0)->hasAlarmState();
    chunk->mDetailVertexCount.setSize(numDetails);

    U32 size = 0;
    for (i = 0; i < numDetails; i++)
    {
        Interior* detail = interior->getDetailLevel(i);

        U32 count = detail->getWindingCount();
        chunk->mDetailVertexCount[i] = count;
        size += count;
    }

    return(true);
}

