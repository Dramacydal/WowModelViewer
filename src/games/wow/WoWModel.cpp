#include "WoWModel.h"

#include <cassert>
#include <algorithm>

#include "Attachment.h"
#include "GlobalSettings.h"
#include "CASCFile.h"
#include "Game.h"
#include "ModelColor.h"
#include "ModelEvent.h"
#include "ModelLight.h"
#include "ModelRenderPass.h"
#include "ModelTransparency.h"

#include "logger/Logger.h"

#include <QXmlStreamWriter>

#include <sstream>

enum TextureFlags
{
  TEXTURE_WRAPX = 1,
  TEXTURE_WRAPY
};

void WoWModel::dumpTextureStatus()
{
  LOG_INFO << "-----------------------------------------";

  for (uint i = 0; i < textures.size(); i++)
    LOG_INFO << "textures[" << i << "] =" << textures[i];

  for (uint i = 0; i < specialTextures.size(); i++)
    LOG_INFO << "specialTextures[" << i << "] =" << specialTextures[i];

  for (uint i = 0; i < replaceTextures.size(); i++)
    LOG_INFO << "replaceTextures[" << i << "] =" << replaceTextures[i];

  LOG_INFO << " #### TEXTUREMANAGER ####";
  TEXTUREMANAGER.dump();
  LOG_INFO << " ########################";

  LOG_INFO << "-----------------------------------------";
}

void
glGetAll()
{
  GLint bled;
  LOG_INFO << "glGetAll Information";
  LOG_INFO << "GL_ALPHA_TEST:" << glIsEnabled(GL_ALPHA_TEST);
  LOG_INFO << "GL_BLEND:" << glIsEnabled(GL_BLEND);
  LOG_INFO << "GL_CULL_FACE:" << glIsEnabled(GL_CULL_FACE);
  glGetIntegerv(GL_FRONT_FACE, &bled);
  if (bled == GL_CW)
  {
    LOG_INFO << "glFrontFace: GL_CW";
  }
  else if (bled == GL_CCW)
  {
    LOG_INFO << "glFrontFace: GL_CCW";
  }
  LOG_INFO << "GL_DEPTH_TEST:" << glIsEnabled(GL_DEPTH_TEST);
  LOG_INFO << "GL_DEPTH_WRITEMASK:" << glIsEnabled(GL_DEPTH_WRITEMASK);
  LOG_INFO << "GL_COLOR_MATERIAL:" << glIsEnabled(GL_COLOR_MATERIAL);
  LOG_INFO << "GL_LIGHT0:" << glIsEnabled(GL_LIGHT0);
  LOG_INFO << "GL_LIGHT1:" << glIsEnabled(GL_LIGHT1);
  LOG_INFO << "GL_LIGHT2:" << glIsEnabled(GL_LIGHT2);
  LOG_INFO << "GL_LIGHT3:" << glIsEnabled(GL_LIGHT3);
  LOG_INFO << "GL_LIGHTING:" << glIsEnabled(GL_LIGHTING);
  LOG_INFO << "GL_TEXTURE_2D:" << glIsEnabled(GL_TEXTURE_2D);
  glGetIntegerv(GL_BLEND_SRC, &bled);
  LOG_INFO << "GL_BLEND_SRC:" << bled;
  glGetIntegerv(GL_BLEND_DST, &bled);
  LOG_INFO << "GL_BLEND_DST:" << bled;
}

void glInitAll()
{
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_COLOR_MATERIAL);
  //glEnable(GL_CULL_FACE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_LIGHT0);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, Vec4D(1.0f, 1.0f, 1.0f, 1.0f));
  glLightfv(GL_LIGHT0, GL_AMBIENT, Vec4D(1.0f, 1.0f, 1.0f, 1.0f));
  glLightfv(GL_LIGHT0, GL_SPECULAR, Vec4D(1.0f, 1.0f, 1.0f, 1.0f));
  glDisable(GL_LIGHT1);
  glDisable(GL_LIGHT2);
  glDisable(GL_LIGHT3);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glBlendFunc(GL_ONE, GL_ZERO);
  glFrontFace(GL_CCW);
  //glDepthMask(GL_TRUE);
  glDepthFunc(GL_NEVER);
}

WoWModel::WoWModel(GameFile * file, bool forceAnim):
ManagedItem(""),
forceAnim(forceAnim),
gamefile(file)
{
  // Initiate our model variables.
  trans = 1.0f;
  rad = 1.0f;
  pos = Vec3D(0.0f, 0.0f, 0.0f);
  rot = Vec3D(0.0f, 0.0f, 0.0f);

  specialTextures.resize(TEXTURE_MAX, -1);
  replaceTextures.resize(TEXTURE_MAX, ModelRenderPass::INVALID_TEX);
  
  for (size_t i = 0; i < ATT_MAX; i++)
    attLookup[i] = -1;

  for (size_t i = 0; i < BONE_MAX; i++)
    keyBoneLookup[i] = -1;

  dlist = 0;

  hasCamera = false;
  hasParticles = false;
  replaceParticleColors = false;
  replacableParticleColorIDs.clear();
  creatureGeosetData.clear();
  isWMO = false;
  isMount = false;

  showModel = false;
  showBones = false;
  showBounds = false;
  showWireframe = false;
  showParticles = false;
  showTexture = true;

  charModelDetails.Reset();

  vbuf = nbuf = tbuf = 0;

  origVertices.clear();
  vertices = 0;
  normals = 0;
  texCoords = 0;
  indices.clear();

  animtime = 0;
  anim = 0;
  animManager = 0;
  currentAnim = 0;
  modelType = MT_NORMAL;
  attachment = 0;

  rawVertices.clear();
  rawIndices.clear();
  rawPasses.clear();
  rawGeosets.clear();

  initCommon(file);
}

WoWModel::~WoWModel()
{
  if (ok)
  {
    if (attachment)
      attachment->setModel(0);

    // There is a small memory leak somewhere with the textures.
    // Especially if the texture was built into the model.
    // No matter what I try though I can't find the memory to unload.
    if (header.nTextures)
    {
      // For character models, the texture isn't loaded into the texture manager, manually remove it
      glDeleteTextures(1, &replaceTextures[1]);
      delete animManager; animManager = 0;

      if (animated)
      {
        // unload all sorts of crap
        // Need this if statement because VBO supported
        // cards have already deleted it.
        if (video.supportVBO)
        {
          glDeleteBuffersARB(1, &nbuf);
          glDeleteBuffersARB(1, &vbuf);
          glDeleteBuffersARB(1, &tbuf);

          vertices = NULL;
        }

        delete[] normals; normals = 0;
        delete[] vertices; vertices = 0;
        delete[] texCoords; texCoords = 0;

        indices.clear();
        rawIndices.clear();
        origVertices.clear();
        rawVertices.clear();
        texAnims.clear();
        colors.clear();
        transparency.clear();
        lights.clear();
        particleSystems.clear();
        ribbons.clear();
        events.clear();

        for (auto it : passes)
          delete it;

        for (auto it : geosets)
          delete it;
      }
      else
      {
        glDeleteLists(dlist, 1);
      }
    }
  }
}


void WoWModel::displayHeader(ModelHeader & a_header)
{
  LOG_INFO << "id:" << a_header.id[0] << a_header.id[1] << a_header.id[2] << a_header.id[3];
  LOG_INFO << "version:" << (int)a_header.version[0] << (int)a_header.version[1] << (int)a_header.version[2] << (int)a_header.version[3];
  LOG_INFO << "nameLength:" << a_header.nameLength;
  LOG_INFO << "nameOfs:" << a_header.nameOfs;
  LOG_INFO << "GlobalModelFlags:" << a_header.GlobalModelFlags;
  LOG_INFO << "nGlobalSequences:" << a_header.nGlobalSequences;
  LOG_INFO << "ofsGlobalSequences:" << a_header.ofsGlobalSequences;
  LOG_INFO << "nAnimations:" << a_header.nAnimations;
  LOG_INFO << "ofsAnimations:" << a_header.ofsAnimations;
  LOG_INFO << "nAnimationLookup:" << a_header.nAnimationLookup;
  LOG_INFO << "ofsAnimationLookup:" << a_header.ofsAnimationLookup;
  LOG_INFO << "nBones:" << a_header.nBones;
  LOG_INFO << "ofsBones:" << a_header.ofsBones;
  LOG_INFO << "nKeyBoneLookup:" << a_header.nKeyBoneLookup;
  LOG_INFO << "ofsKeyBoneLookup:" << a_header.ofsKeyBoneLookup;
  LOG_INFO << "nVertices:" << a_header.nVertices;
  LOG_INFO << "ofsVertices:" << a_header.ofsVertices;
  LOG_INFO << "nViews:" << a_header.nViews;
  LOG_INFO << "nColors:" << a_header.nColors;
  LOG_INFO << "ofsColors:" << a_header.ofsColors;
  LOG_INFO << "nTextures:" << a_header.nTextures;
  LOG_INFO << "ofsTextures:" << a_header.ofsTextures;
  LOG_INFO << "nTransparency:" << a_header.nTransparency;
  LOG_INFO << "ofsTransparency:" << a_header.ofsTransparency;
  LOG_INFO << "nTexAnims:" << a_header.nTexAnims;
  LOG_INFO << "ofsTexAnims:" << a_header.ofsTexAnims;
  LOG_INFO << "nTexReplace:" << a_header.nTexReplace;
  LOG_INFO << "ofsTexReplace:" << a_header.ofsTexReplace;
  LOG_INFO << "nTexFlags:" << a_header.nTexFlags;
  LOG_INFO << "ofsTexFlags:" << a_header.ofsTexFlags;
  LOG_INFO << "nBoneLookup:" << a_header.nBoneLookup;
  LOG_INFO << "ofsBoneLookup:" << a_header.ofsBoneLookup;
  LOG_INFO << "nTexLookup:" << a_header.nTexLookup;
  LOG_INFO << "ofsTexLookup:" << a_header.ofsTexLookup;
  LOG_INFO << "nTexUnitLookup:" << a_header.nTexUnitLookup;
  LOG_INFO << "ofsTexUnitLookup:" << a_header.ofsTexUnitLookup;
  LOG_INFO << "nTransparencyLookup:" << a_header.nTransparencyLookup;
  LOG_INFO << "ofsTransparencyLookup:" << a_header.ofsTransparencyLookup;
  LOG_INFO << "nTexAnimLookup:" << a_header.nTexAnimLookup;
  LOG_INFO << "ofsTexAnimLookup:" << a_header.ofsTexAnimLookup;

  //	LOG_INFO << "collisionSphere :";
  //	displaySphere(a_header.collisionSphere);
  //	LOG_INFO << "boundSphere :";
  //	displaySphere(a_header.boundSphere);

  LOG_INFO << "nBoundingTriangles:" << a_header.nBoundingTriangles;
  LOG_INFO << "ofsBoundingTriangles:" << a_header.ofsBoundingTriangles;
  LOG_INFO << "nBoundingVertices:" << a_header.nBoundingVertices;
  LOG_INFO << "ofsBoundingVertices:" << a_header.ofsBoundingVertices;
  LOG_INFO << "nBoundingNormals:" << a_header.nBoundingNormals;
  LOG_INFO << "ofsBoundingNormals:" << a_header.ofsBoundingNormals;

  LOG_INFO << "nAttachments:" << a_header.nAttachments;
  LOG_INFO << "ofsAttachments:" << a_header.ofsAttachments;
  LOG_INFO << "nAttachLookup:" << a_header.nAttachLookup;
  LOG_INFO << "ofsAttachLookup:" << a_header.ofsAttachLookup;
  LOG_INFO << "nEvents:" << a_header.nEvents;
  LOG_INFO << "ofsEvents:" << a_header.ofsEvents;
  LOG_INFO << "nLights:" << a_header.nLights;
  LOG_INFO << "ofsLights:" << a_header.ofsLights;
  LOG_INFO << "nCameras:" << a_header.nCameras;
  LOG_INFO << "ofsCameras:" << a_header.ofsCameras;
  LOG_INFO << "nCameraLookup:" << a_header.nCameraLookup;
  LOG_INFO << "ofsCameraLookup:" << a_header.ofsCameraLookup;
  LOG_INFO << "nRibbonEmitters:" << a_header.nRibbonEmitters;
  LOG_INFO << "ofsRibbonEmitters:" << a_header.ofsRibbonEmitters;
  LOG_INFO << "nParticleEmitters:" << a_header.nParticleEmitters;
  LOG_INFO << "ofsParticleEmitters:" << a_header.ofsParticleEmitters;
}


bool WoWModel::isAnimated(GameFile * f)
{
  // see if we have any animated bones
  ModelBoneDef *bo = (ModelBoneDef*)(f->getBuffer() + header.ofsBones);

  animGeometry = false;
  animBones = false;
  ind = false;
  
  for (auto ov_it = origVertices.begin(), ov_end = origVertices.end(); (ov_it != ov_end) && !animGeometry; ++ov_it)
  {
    for (size_t b = 0; b < 4; b++)
    {
      if (ov_it->weights[b]>0)
      {
        ModelBoneDef &bb = bo[ov_it->bones[b]];
        if (bb.translation.type || bb.rotation.type || bb.scaling.type || (bb.flags & MODELBONE_BILLBOARD))
        {
          if (bb.flags & MODELBONE_BILLBOARD)
          {
            // if we have billboarding, the model will need per-instance animation
            ind = true;
          }
          animGeometry = true;
          break;
        }
      }
    }
  }

  if (animGeometry)
  {
    animBones = true;
  }
  else
  {
    for (uint i = 0; i < bones.size() ; i++)
    {
      ModelBoneDef &bb = bo[i];
      if (bb.translation.type || bb.rotation.type || bb.scaling.type)
      {
        animBones = true;
        animGeometry = true;
        break;
      }
    }
  }

  bool animMisc = header.nCameras > 0 || // why waste time, pretty much all models with cameras need animation
    header.nLights > 0 || // same here
    header.nParticleEmitters > 0 ||
    header.nRibbonEmitters > 0;

  if (animMisc)
    animBones = true;

  // animated colors
  if (header.nColors)
  {
    ModelColorDef *cols = (ModelColorDef*)(f->getBuffer() + header.ofsColors);
    for (size_t i = 0; i < header.nColors; i++)
    {
      if (cols[i].color.type != 0 || cols[i].opacity.type != 0)
      {
        animMisc = true;
        break;
      }
    }
  }

  // animated opacity
  if (header.nTransparency && !animMisc)
  {
    ModelTransDef *trs = (ModelTransDef*)(f->getBuffer() + header.ofsTransparency);
    for (size_t i = 0; i < header.nTransparency; i++)
    {
      if (trs[i].trans.type != 0)
      {
        animMisc = true;
        break;
      }
    }
  }

  // guess not...
  return animGeometry || (header.nTexAnims > 0) || animMisc;
}

void WoWModel::initCommon(GameFile * f)
{
  // --
  ok = false;

  if (!f)
    return;

  if (!f->open() || f->isEof() || (f->getSize() < sizeof(ModelHeader)))
  {
    LOG_ERROR << "Unable to load model:" << f->fullname();
    f->close();
    return;
  }

  if (f->isChunked() && !f->setChunk("MD21")) // Legion chunked files
  {
    LOG_ERROR << "Unable to set chunk to MD21 for model:" << f->fullname();
    f->close();
    return;
  }

  setItemName(f->fullname());

  // replace .MDX with .M2
  QString tempname = f->fullname();
  tempname.replace(".mdx", ".m2");

  ok = true;

  memcpy(&header, f->getBuffer(), sizeof(ModelHeader));

  LOG_INFO << "Loading model:" << tempname << "size:" << f->getSize();

  // displayHeader(header);

  if (header.id[0] != 'M' && header.id[1] != 'D' && header.id[2] != '2' && header.id[3] != '0')
  {
    LOG_ERROR << "Invalid model!  May be corrupted. Header id:" << header.id[0] << header.id[1] << header.id[2] << header.id[3];

    ok = false;
    f->close();
    return;
  }

  if (header.GlobalModelFlags & 0x200000)
    model24500 = true;

  modelname = tempname.toStdString();
  QStringList list = tempname.split("\\");
  setName(list[list.size() - 1].replace(".m2", ""));

  // Error check
  // 10 1 0 0 = WoW 5.0 models (as of 15464)
  // 10 1 0 0 = WoW 4.0.0.12319 models
  // 9 1 0 0 = WoW 4.0 models
  // 8 1 0 0 = WoW 3.0 models
  // 4 1 0 0 = WoW 2.0 models
  // 0 1 0 0 = WoW 1.0 models

  if (f->getSize() < header.ofsParticleEmitters)
  {
    LOG_ERROR << "Unable to load the Model \"" << tempname << "\", appears to be corrupted.";
    f->close();
    return;
  }

  if (f->isChunked() && f->setChunk("SKID"))
  {
    uint32 skelFileID;
    f->read(&skelFileID, sizeof(skelFileID));
    GameFile * skelFile = GAMEDIRECTORY.getFile(skelFileID);

    if (skelFile->open())
    {
      if (skelFile->setChunk("SKS1"))
      {
        SKS1 sks1;
        memcpy(&sks1, skelFile->getBuffer(), sizeof(SKS1));

        if (sks1.nGlobalSequences > 0)
        {
          // vector.assign() isn't working:
          // globalSequences.assign(skelFile->getBuffer() + sks1.ofsGlobalSequences, skelFile->getBuffer() + sks1.ofsGlobalSequences + sks1.nGlobalSequences);
          uint32 * buffer = new uint32[sks1.nGlobalSequences];
          memcpy(buffer, skelFile->getBuffer() + sks1.ofsGlobalSequences, sizeof(uint32)*sks1.nGlobalSequences);
          globalSequences.assign(buffer, buffer + sks1.nGlobalSequences);
          delete[] buffer;
        }

        // let's try to read parent skel file if needed
        if (skelFile->setChunk("SKPD"))
        {
          SKPD skpd;
          memcpy(&skpd, skelFile->getBuffer(), sizeof(SKPD));

          GameFile * parentFile = GAMEDIRECTORY.getFile(skpd.parentFileId);

          if (parentFile && parentFile->open() && parentFile->setChunk("SKS1"))
          {
            SKS1 sks1;
            memcpy(&sks1, parentFile->getBuffer(), sizeof(SKS1));

            if (sks1.nGlobalSequences > 0)
            {
              uint32 * buffer = new uint32[sks1.nGlobalSequences];
              memcpy(buffer, parentFile->getBuffer() + sks1.ofsGlobalSequences, sizeof(uint32)*sks1.nGlobalSequences);
              for (uint i = 0; i < sks1.nGlobalSequences; i++)
                globalSequences.push_back(buffer[i]);
            }

            parentFile->close();
          }
        }

      }
      skelFile->close();
    }
    f->setChunk("MD21");
  }
  else if (header.nGlobalSequences)
  {
    // vector.assign() isn't working:
    // globalSequences.assign(f->getBuffer() + header.ofsGlobalSequences, f->getBuffer() + header.ofsGlobalSequences + header.nGlobalSequences);
    uint32 * buffer = new uint32[header.nGlobalSequences];
    memcpy(buffer, f->getBuffer() + header.ofsGlobalSequences, sizeof(uint32)*header.nGlobalSequences);
    globalSequences.assign(buffer, buffer + header.nGlobalSequences);
    delete[] buffer;
  }

  if (f->isChunked() && f->setChunk("SFID"))
  {
    uint32 skinfile;
    
    if (header.nViews > 0)
    { 
      for (uint i = 0; i < header.nViews; i++)
      {
        f->read(&skinfile, sizeof(skinfile));
        skinFileIDs.push_back(skinfile);
        LOG_INFO << "Adding skin file" << i << ":" << skinfile;
        // If the first view is the best, and we don't need to switch to a lower one, then maybe we don't need to store all these file IDs, but we can for now.
      }
    }
    // LOD .skin file IDs are next in SFID, but we'll ignore them. They're probably unnecessary in a model viewer.

    f->setChunk("MD21");
  }

  if (forceAnim)
    animBones = true;

  // Ready to render.
  showModel = true;
  alpha = 1.0f;

  ModelVertex * buffer = new ModelVertex[header.nVertices];
  memcpy(buffer, f->getBuffer() + header.ofsVertices, sizeof(ModelVertex)*header.nVertices);
  rawVertices.assign(buffer, buffer + header.nVertices);
  delete[] buffer;

  // Correct the data from the model, so that its using the Y-Up axis mode.
  for (auto & it : rawVertices)
  {
     it.pos = fixCoordSystem(it.pos);
     it.normal = fixCoordSystem(it.normal);
  }

  origVertices = rawVertices;

  // This data is needed for both VBO and non-VBO cards.
  vertices = new Vec3D[origVertices.size()];
  normals = new Vec3D[origVertices.size()];

  uint i = 0;
  for (auto ov_it = origVertices.begin(), ov_end = origVertices.end(); ov_it != ov_end; i++, ov_it++)
  {
    // Set the data for our vertices, normals from the model data
    vertices[i] = ov_it->pos;
    normals[i] = ov_it->normal.normalize();

    float len = ov_it->pos.lengthSquared();
    if (len > rad)
    {
      rad = len;
    }
  }

  // model vertex radius
  rad = sqrtf(rad);

  // bounds
  if (header.nBoundingVertices > 0)
  {
    Vec3D * buffer = new Vec3D[header.nBoundingVertices];
    memcpy(buffer, f->getBuffer() + header.ofsBoundingVertices, sizeof(Vec3D)*header.nBoundingVertices);
    bounds.assign(buffer, buffer + header.nBoundingVertices);
    delete[] buffer;

    for (uint i = 0; i < bounds.size(); i++)
      bounds[i] = fixCoordSystem(bounds[i]);
  }

  if (header.nBoundingTriangles > 0)
  {
    uint16 * buffer = new uint16[header.nBoundingTriangles];
    memcpy(buffer, f->getBuffer() + header.ofsBoundingTriangles, sizeof(uint16)*header.nBoundingTriangles);
    boundTris.assign(buffer, buffer + header.nBoundingTriangles);
    delete[] buffer;
  }

  // textures
  ModelTextureDef *texdef = (ModelTextureDef*)(f->getBuffer() + header.ofsTextures);
  if (header.nTextures)
  {
    textures.resize(TEXTURE_MAX, ModelRenderPass::INVALID_TEX);

    vector<TXID> txids;

    if (f->isChunked() && f->setChunk("TXID"))
    {
      txids = readTXIDSFromFile(f);
      f->setChunk("MD21", false);
    }

    for (size_t i = 0; i < header.nTextures; i++)
    {
      /*
      Texture Types
      Texture type is 0 for textures whose file IDs or names are contained in the the model file.
      The older implementation has full file paths, but the newer uses file data IDs
      contained in a TXID chunk. We have to support both for now.

      All other texture types (nonzero) are for textures that are obtained from other files.
      For instance, in the NightElfFemale model, her eye glow is a type 0 texture and has a
      file name. Her other 3 textures have types of 1, 2 and 6. The texture filenames for these
      come from client database files:

      DBFilesClient\CharSections.dbc
      DBFilesClient\CreatureDisplayInfo.dbc
      DBFilesClient\ItemDisplayInfo.dbc
      (possibly more)

      0	 Texture given in filename
      1	 Body + clothes
      2	 Cape
      6	 Hair, beard
      8	 Tauren fur
      11 Skin for creatures #1
      12 Skin for creatures #2
      13 Skin for creatures #3

      Texture Flags
      Value	 Meaning
      1	Texture wrap X
      2	Texture wrap Y
      */

      if (texdef[i].type == TEXTURE_FILENAME)  // 0
      {
        GameFile * tex;
        if (txids.size() > 0)
        {
          tex = GAMEDIRECTORY.getFile(txids[i].fileDataId);
        }
        else
        {
          QString texname((char*)(f->getBuffer() + texdef[i].nameOfs));
          tex = GAMEDIRECTORY.getFile(texname);
        }
        textures[i] = TEXTUREMANAGER.add(tex);
      }
      else  // non-zero
      {
        // special texture - only on characters and such...
        specialTextures[i] = texdef[i].type;

        if (texdef[i].type == TEXTURE_ARMORREFLECT) // a fix for weapons with type-3 textures.
          replaceTextures[texdef[i].type] = TEXTUREMANAGER.add(GAMEDIRECTORY.getFile("Item\\ObjectComponents\\Weapon\\ArmorReflect4.BLP"));
      }
    }
  }

  /*
  // replacable textures - it seems to be better to get this info from the texture types
  if (header.nTexReplace) {
  size_t m = header.nTexReplace;
  if (m>16) m = 16;
  int16 *texrep = (int16*)(f->getBuffer() + header.ofsTexReplace);
  for (size_t i=0; i<m; i++) specialTextures[i] = texrep[i];
  }
  */

  if (f->isChunked() && f->setChunk("SKID"))
  {
    uint32 skelFileID;
    f->read(&skelFileID, sizeof(skelFileID));
    GameFile * skelFile = GAMEDIRECTORY.getFile(skelFileID);

    if (skelFile->open())
    {
      if (skelFile->setChunk("SKA1"))
      {
        SKA1 ska1;
        memcpy(&ska1, skelFile->getBuffer(), sizeof(SKA1));
        header.nAttachments = ska1.nAttachments;
        ModelAttachmentDef *attachments = (ModelAttachmentDef*)(skelFile->getBuffer() + ska1.ofsAttachments);
        for (size_t i = 0; i < ska1.nAttachments; i++)
        {
          ModelAttachment att;
          att.model = this;
          att.init(attachments[i]);
          atts.push_back(att);
        }

        header.nAttachLookup = ska1.nAttachLookup;
        if (ska1.nAttachLookup > 0)
        {
          int16 *p = (int16*)(skelFile->getBuffer() + ska1.ofsAttachLookup);
          if (ska1.nAttachLookup > ATT_MAX)
            LOG_ERROR << "Model AttachLookup" << ska1.nAttachLookup << "over" << ATT_MAX;
          for (size_t i = 0; i < ska1.nAttachLookup; i++)
          {
            if (i > ATT_MAX - 1)
              break;
            attLookup[i] = p[i];
          }
        }
      }
      skelFile->close();
    }
    f->setChunk("MD21");
  }
  else
  {
    // attachments
    if (header.nAttachments)
    {
      ModelAttachmentDef *attachments = (ModelAttachmentDef*)(f->getBuffer() + header.ofsAttachments);
      for (size_t i = 0; i < header.nAttachments; i++)
      {
        ModelAttachment att;
        att.model = this;
        att.init(attachments[i]);
        atts.push_back(att);
      }
    }

    if (header.nAttachLookup)
    {
      int16 *p = (int16*)(f->getBuffer() + header.ofsAttachLookup);
      if (header.nAttachLookup > ATT_MAX)
        LOG_ERROR << "Model AttachLookup" << header.nAttachLookup << "over" << ATT_MAX;
      for (size_t i = 0; i < header.nAttachLookup; i++)
      {
        if (i > ATT_MAX - 1)
          break;
        attLookup[i] = p[i];
      }
    }
  }


  // init colors
  if (header.nColors)
  {
    colors.resize(header.nColors);
    ModelColorDef *colorDefs = (ModelColorDef*)(f->getBuffer() + header.ofsColors);
    for (uint i = 0; i < colors.size(); i++)
      colors[i].init(f, colorDefs[i], globalSequences);
  }

  // init transparency
  if (header.nTransparency)
  {
    transparency.resize(header.nTransparency);
    ModelTransDef *trDefs = (ModelTransDef*)(f->getBuffer() + header.ofsTransparency);
    for (uint i = 0; i < header.nTransparency; i++)
      transparency[i].init(f, trDefs[i], globalSequences);
  }

  if (header.nViews)
  {
    // just use the first LOD/view
    // First LOD/View being the worst?
    // TODO: Add support for selecting the LOD.
    // int viewLOD = 0; // sets LOD to worst
    // int viewLOD = header.nViews - 1; // sets LOD to best
    setLOD(f, 0); // Set the default Level of Detail to the best possible.
  }

  // proceed with specialized init depending on model "type"

  animated = isAnimated(f) || forceAnim;  // isAnimated will set animGeometry

  if (animated)
    initAnimated(f);
  else
    initStatic(f);

  f->close();
}

void WoWModel::initStatic(GameFile * f)
{
  dlist = glGenLists(1);
  glNewList(dlist, GL_COMPILE);

  drawModel();

  glEndList();

  // clean up vertices, indices etc
  delete[] vertices; vertices = 0;
  delete[] normals; normals = 0;
  indices.clear();
}

vector<TXID> WoWModel::readTXIDSFromFile(GameFile * f)
{
  vector<TXID> txids;

  if (f->setChunk("TXID"))
  {
    TXID txid;
    while (!f->isEof())
    {
      f->read(&txid, sizeof(TXID));
      txids.push_back(txid);
    }
  }
  return txids;
}

vector<AFID> WoWModel::readAFIDSFromFile(GameFile * f)
{
  vector<AFID> afids;

  if (f->setChunk("AFID"))
  {
    AFID afid;
    while (!f->isEof())
    {
      f->read(&afid, sizeof(AFID));
      if (afid.fileId != 0)
        afids.push_back(afid);
    }
  }

  return afids;
}

void WoWModel::readAnimsFromFile(GameFile * f, vector<AFID> & afids, modelAnimData & data, uint32 nAnimations, uint32 ofsAnimation, uint32 nAnimationLookup, uint32 ofsAnimationLookup)
{
  for (uint i = 0; i < nAnimations; i++)
  {
    ModelAnimation a;
    memcpy(&a, f->getBuffer() + ofsAnimation + i*sizeof(ModelAnimation), sizeof(ModelAnimation));

    anims.push_back(a);

    GameFile * anim = 0;

    // if we have animation file ids from AFID chunk, use them
    if (afids.size() > 0)
    {
      for (auto it : afids)
      {
        if ((it.animId == anims[i].animID) && (it.subAnimId == anims[i].subAnimID))
        {
          anim = GAMEDIRECTORY.getFile(it.fileId);
          break;
        }
      }
    }
    else // else use file naming to get them
    {
      QString tempname = QString::fromStdString(modelname).replace(".m2", "");
      tempname = QString("%1%2-%3.anim").arg(tempname).arg(anims[i].animID, 4, 10, QChar('0')).arg(anims[i].subAnimID, 2, 10, QChar('0'));
      anim = GAMEDIRECTORY.getFile(tempname);
    }

    if (anim && anim->open())
    {
      anim->setChunk("AFSB"); // try to set chunk if it exist, no effect if there is no AFSB chunk present
      {     
        auto animIt = data.animfiles.find(anims[i].animID);
        if (animIt != data.animfiles.end())
          LOG_INFO << "WARNING - replacing" << data.animfiles[anims[i].animID].first->fullname() << "by" << anim->fullname();
      }
      
      data.animfiles[anims[i].animID] = std::make_pair(anim, f);
    }
  }

  // Index at ofsAnimations which represents the animation in AnimationData.dbc. -1 if none.
  if (nAnimationLookup > 0)
  {
    // for unknown reason, using assign() on vector doesn't work
    // use intermediate buffer and push back instead...
    int16 * buffer = new int16[nAnimationLookup];
    memcpy(buffer, f->getBuffer() + ofsAnimationLookup, sizeof(int16)*nAnimationLookup);
    for (uint i = 0; i < nAnimationLookup; i++)
      animLookups.push_back(buffer[i]);

    delete[] buffer;
  }
}


void WoWModel::initAnimated(GameFile * f)
{
  modelAnimData data;
  data.globalSequences = globalSequences;

  if (f->isChunked() && f->setChunk("SKID"))
  {
    uint32 skelFileID;
    f->read(&skelFileID, sizeof(skelFileID));
    GameFile * skelFile = GAMEDIRECTORY.getFile(skelFileID);

    if (skelFile->open())
    {
      // skelFile->dumpStructure();
      vector<AFID> afids = readAFIDSFromFile(skelFile);

      if (skelFile->setChunk("SKS1"))
      {
        SKS1 sks1;

        // let's try if there is a parent skel file to read
        GameFile * parentFile = 0;
        if (skelFile->setChunk("SKPD"))
        {
          SKPD skpd;
          skelFile->read(&skpd, sizeof(skpd));

          parentFile = GAMEDIRECTORY.getFile(skpd.parentFileId);

          if (parentFile && parentFile->open())
          {
            // parentFile->dumpStructure();
            afids = readAFIDSFromFile(parentFile);

            if (parentFile->setChunk("SKS1"))
            {
              SKS1 sks1;
              parentFile->read(&sks1, sizeof(sks1));
              readAnimsFromFile(parentFile, afids, data, sks1.nAnimations, sks1.ofsAnimations, sks1.nAnimationLookup, sks1.ofsAnimationLookup);
            }

            parentFile->close();
          }
        }
        else
        {
          skelFile->read(&sks1, sizeof(sks1));
          memcpy(&sks1, skelFile->getBuffer(), sizeof(SKS1));
          readAnimsFromFile(skelFile, afids, data, sks1.nAnimations, sks1.ofsAnimations, sks1.nAnimationLookup, sks1.ofsAnimationLookup);
        }

        animManager = new AnimManager(*this);
        
        // init bones...
        if (skelFile->setChunk("SKB1"))
        {
          GameFile * fileToUse = skelFile;
          if (parentFile)
          {
            parentFile->open();
            parentFile->setChunk("SKB1");
            skelFile->close();
            fileToUse = parentFile;
          }

          SKB1 skb1;
          fileToUse->read(&skb1, sizeof(skb1));
          memcpy(&skb1, fileToUse->getBuffer(), sizeof(SKB1));
          bones.resize(skb1.nBones);
          ModelBoneDef *mb = (ModelBoneDef*)(fileToUse->getBuffer() + skb1.ofsBones);

          for (uint i = 0; i < anims.size(); i++)
            data.animIndexToAnimId[i] = anims[i].animID;

          for (size_t i = 0; i < skb1.nBones; i++)
            bones[i].initV3(*fileToUse, mb[i], data);

          // Block keyBoneLookup is a lookup table for Key Skeletal Bones, hands, arms, legs, etc.
          if (skb1.nKeyBoneLookup < BONE_MAX)
          {
            memcpy(keyBoneLookup, fileToUse->getBuffer() + skb1.ofsKeyBoneLookup, sizeof(int16)*skb1.nKeyBoneLookup);
          }
          else
          {
            memcpy(keyBoneLookup, fileToUse->getBuffer() + skb1.ofsKeyBoneLookup, sizeof(int16)*BONE_MAX);
            LOG_ERROR << "KeyBone number" << skb1.nKeyBoneLookup << "over" << BONE_MAX;
          }
          fileToUse->close();
        }
      }
      skelFile->close();
    }
    f->setChunk("MD21", false);
  }
  else if (header.nAnimations > 0)
  {
    vector<AFID> afids;

    if (f->isChunked() && f->setChunk("AFID"))
    {
      afids = readAFIDSFromFile(f);
      f->setChunk("MD21", false);
    }

    readAnimsFromFile(f, afids, data, header.nAnimations, header.ofsAnimations, header.nAnimationLookup, header.ofsAnimationLookup);

    animManager = new AnimManager(*this);

    // init bones...
    bones.resize(header.nBones);
    ModelBoneDef *mb = (ModelBoneDef*)(f->getBuffer() + header.ofsBones);

    for (uint i = 0; i < anims.size(); i++)
      data.animIndexToAnimId[i] = anims[i].animID;

    for (uint i = 0; i < bones.size(); i++)
      bones[i].initV3(*f, mb[i], data);

    // Block keyBoneLookup is a lookup table for Key Skeletal Bones, hands, arms, legs, etc.
    if (header.nKeyBoneLookup < BONE_MAX)
    {
      memcpy(keyBoneLookup, f->getBuffer() + header.ofsKeyBoneLookup, sizeof(int16)*header.nKeyBoneLookup);
    }
    else
    {
      memcpy(keyBoneLookup, f->getBuffer() + header.ofsKeyBoneLookup, sizeof(int16)*BONE_MAX);
      LOG_ERROR << "KeyBone number" << header.nKeyBoneLookup << "over" << BONE_MAX;
    }
  }

  // free MPQFile
  for (auto it : data.animfiles)
  {
    if (it.second.first != 0)
      it.second.first->close();
  }

  const size_t size = (origVertices.size() * sizeof(float));
  vbufsize = (3 * size); // we multiple by 3 for the x, y, z positions of the vertex

  texCoords = new Vec2D[origVertices.size()];
  auto ov_it = origVertices.begin();
  for (size_t i = 0; i < origVertices.size(); i++, ++ov_it)
    texCoords[i] = ov_it->texcoords;

  if (video.supportVBO)
  {
    // Vert buffer
    glGenBuffersARB(1, &vbuf);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, vertices, GL_STATIC_DRAW_ARB);
    delete[] vertices; vertices = 0;

    // Texture buffer
    glGenBuffersARB(1, &tbuf);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, tbuf);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, 2 * size, texCoords, GL_STATIC_DRAW_ARB);
    delete[] texCoords; texCoords = 0;

    // normals buffer
    glGenBuffersARB(1, &nbuf);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, nbuf);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, normals, GL_STATIC_DRAW_ARB);
    delete[] normals; normals = 0;

    // clean bind
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
  }

  if (header.nTexAnims > 0)
  {
    texAnims.resize(header.nTexAnims);
    ModelTexAnimDef *ta = (ModelTexAnimDef*)(f->getBuffer() + header.ofsTexAnims);
                                                                 
    for (uint i = 0; i < texAnims.size(); i++)
      texAnims[i].init(f, ta[i], globalSequences);
  }

  if (header.nEvents)
  {
    ModelEventDef *edefs = (ModelEventDef *)(f->getBuffer() + header.ofsEvents);
    events.resize(header.nEvents);
    for (uint i = 0; i < events.size(); i++)
      events[i].init(edefs[i]);
  }

  // particle systems
  if (header.nParticleEmitters)
  {
    M2ParticleDef *pdefs = (M2ParticleDef *)(f->getBuffer() + header.ofsParticleEmitters);
    M2ParticleDef *pdef;
    particleSystems.resize(header.nParticleEmitters);
    hasParticles = true;
    showParticles = true;
    for (uint i = 0; i < particleSystems.size(); i++)
    {
      pdef = (M2ParticleDef *)&pdefs[i];
      particleSystems[i].model = this;
      particleSystems[i].init(f, *pdef, globalSequences);
      int pci = particleSystems[i].particleColID;
      if (pci && (std::find(replacableParticleColorIDs.begin(),
        replacableParticleColorIDs.end(), pci) == replacableParticleColorIDs.end()))
        replacableParticleColorIDs.push_back(pci);
    }
  }

  // ribbons
  if (header.nRibbonEmitters)
  {
    ModelRibbonEmitterDef *rdefs = (ModelRibbonEmitterDef *)(f->getBuffer() + header.ofsRibbonEmitters);
    ribbons.resize(header.nRibbonEmitters);
    for (uint i = 0; i < ribbons.size(); i++)
    {
      ribbons[i].model = this;
      ribbons[i].init(f, rdefs[i], globalSequences);
    }
  }

  // Cameras
  if (header.nCameras > 0)
  {
    if (header.version[0] <= 9)
    {
      ModelCameraDef *camDefs = (ModelCameraDef*)(f->getBuffer() + header.ofsCameras);
      for (size_t i = 0; i < header.nCameras; i++)
      {
        ModelCamera a;
        a.init(f, camDefs[i], globalSequences, modelname);
        cam.push_back(a);
      }
    }
    else if (header.version[0] <= 16)
    {
      ModelCameraDefV10 *camDefs = (ModelCameraDefV10*)(f->getBuffer() + header.ofsCameras);
      for (size_t i = 0; i < header.nCameras; i++)
      {
        ModelCamera a;
        a.initv10(f, camDefs[i], globalSequences, modelname);
        cam.push_back(a);
      }
    }
    if (cam.size() > 0)
    {
      hasCamera = true;
    }
  }

  // init lights
  if (header.nLights)
  {
    lights.resize(header.nLights);
    ModelLightDef *lDefs = (ModelLightDef*)(f->getBuffer() + header.ofsLights);
    for (uint i = 0; i < lights.size(); i++)
      lights[i].init(f, lDefs[i], globalSequences);
  }

  animcalc = false;
}

void WoWModel::setLOD(GameFile * f, int index)
{
  GameFile * g;
  
  if (f->isChunked())
  {
    int numSkinFiles = sizeof(skinFileIDs);
    if (!numSkinFiles)
    {
      LOG_ERROR << "Attempt to set view level when no .skin files exist.";
      return;
    }
    
    if (index < 0)
    {
      index = 0;
      LOG_ERROR << "Attempt to set view level to negative number (" << index << ").";
    }
    else if (index >= numSkinFiles)
    {
      index = numSkinFiles - 1;
      LOG_ERROR << "Attempt to set view level too high (" << index << "). Setting LOD to valid max (" << index << ").";
    }
  
    uint32 skinfile = skinFileIDs[index];
    g = GAMEDIRECTORY.getFile(skinfile);
    if (!g || !g->open())
    {
      LOG_ERROR << "Unable to load .skin file with ID" << skinfile << ".";
      return;
    }
  }
  else
  {
    QString tmpname = QString::fromStdString(modelname).replace(".m2", "", Qt::CaseInsensitive);
    lodname = QString("%1%2.skin").arg(tmpname).arg(index, 2, 10, QChar('0')).toStdString(); // Lods: 00, 01, 02, 03

    g = GAMEDIRECTORY.getFile(lodname.c_str());
    if (!g || !g->open())
    {
      LOG_ERROR << "Unable to load .skin file:" << lodname.c_str();
      return;
    }  
  }
  
  // Texture definitions
  ModelTextureDef *texdef = (ModelTextureDef*)(f->getBuffer() + header.ofsTextures);

  // Transparency
  int16 *transLookup = (int16*)(f->getBuffer() + header.ofsTransparencyLookup);

  if (g->isEof())
  {
    LOG_ERROR << "Unable to load .skin file:" << g->fullname() << ", ID:" << g->fileDataId();
    g->close();
    return;
  }

  ModelView *view = (ModelView*)(g->getBuffer());

  if (view->id[0] != 'S' || view->id[1] != 'K' || view->id[2] != 'I' || view->id[3] != 'N')
  {
    LOG_ERROR << "Doesn't appear to be .skin file:" << g->fullname() << ", ID:" << g->fileDataId();
    g->close();
    return;
  }

  // Indices,  Triangles
  uint16 *indexLookup = (uint16*)(g->getBuffer() + view->ofsIndex);
  uint16 *triangles = (uint16*)(g->getBuffer() + view->ofsTris);
  rawIndices.clear();
  rawIndices.resize(view->nTris);

  for (size_t i = 0; i < view->nTris; i++)
  {
    rawIndices[i] = indexLookup[triangles[i]];
  }

  indices = rawIndices;

  // render ops
  ModelGeoset *ops = (ModelGeoset*)(g->getBuffer() + view->ofsSub);
  ModelTexUnit *tex = (ModelTexUnit*)(g->getBuffer() + view->ofsTex);
  ModelRenderFlags *renderFlags = (ModelRenderFlags*)(f->getBuffer() + header.ofsTexFlags);
  uint16 *texlookup = (uint16*)(f->getBuffer() + header.ofsTexLookup);
  uint16 *texanimlookup = (uint16*)(f->getBuffer() + header.ofsTexAnimLookup);
  int16 *texunitlookup = (int16*)(f->getBuffer() + header.ofsTexUnitLookup);

  uint32 istart = 0;
  for (size_t i = 0; i < view->nSub; i++)
  {
    ModelGeosetHD * hdgeo = new ModelGeosetHD(ops[i]);
    hdgeo->istart = istart;
    istart += hdgeo->icount;
    hdgeo->display = (hdgeo->id == 0);
    rawGeosets.push_back(hdgeo);
  }

  restoreRawGeosets();

  rawPasses.clear();
 
  for (size_t j = 0; j < view->nTex; j++)
  {
    ModelRenderPass * pass = new ModelRenderPass(this, tex[j].op);

    uint texOffset = 0;
    uint texCount = tex[j].op_count;
    // THIS IS A QUICK AND DIRTY WORKAROUND. If op_count > 1 then the texture unit contains multiple textures.
    // Properly we should display them all, blended, but WMV doesn't support that yet, and it ends up
    // displaying one randomly. So for now we try to guess which one is the most important by checking
    // if any are special textures (11, 12 or 13). If so, we choose the first one that fits this criterion.
    pass->specialTex = specialTextures[texlookup[tex[j].textureid]];
    for (size_t k = 0; k < texCount; k++)
    {
      int special = specialTextures[texlookup[tex[j].textureid + k]];
      if (special == 11 || special == 12 || special == 13)
      {
        texOffset = k;
        pass->specialTex = special;
        if (texCount > 1)
          LOG_INFO << "setLOD: texture unit"<<j<<"has" << texCount << "textures. Choosing texture"<<k+1<<", which has special type ="<<special;
        break;
      }
    }
    pass->tex = texlookup[tex[j].textureid + texOffset];
 
    // TODO: figure out these flags properly -_-
    ModelRenderFlags &rf = renderFlags[tex[j].flagsIndex];

    pass->blendmode = rf.blend;
    //if (rf.blend == 0) // Test to disable/hide different blend types
    //	continue;

    pass->color = tex[j].colorIndex;

    pass->opacity = transLookup[tex[j].transid + texOffset];

    pass->unlit = (rf.flags & RENDERFLAGS_UNLIT) != 0;

    pass->cull = (rf.flags & RENDERFLAGS_TWOSIDED) == 0;

    pass->billboard = (rf.flags & RENDERFLAGS_BILLBOARD) != 0;

    // Use environmental reflection effects?
    pass->useEnvMap = (texunitlookup[tex[j].texunit] == -1) && pass->billboard && rf.blend > 2; //&& rf.blend<5;

    // Disable environmental mapping if its been unchecked.
    if (pass->useEnvMap && !video.useEnvMapping)
      pass->useEnvMap = false;

    pass->noZWrite = (rf.flags & RENDERFLAGS_ZBUFFERED) != 0;

    // ToDo: Work out the correct way to get the true/false of transparency
    pass->trans = (pass->blendmode > 0) && (pass->opacity > 0);	// Transparency - not the correct way to get transparency

    // Texture flags
    pass->swrap = (texdef[pass->tex].flags & TEXTURE_WRAPX) != 0; // Texture wrap X
    pass->twrap = (texdef[pass->tex].flags & TEXTURE_WRAPY) != 0; // Texture wrap Y

    // tex[j].flags: Usually 16 for static textures, and 0 for animated textures.
    if ((tex[j].flags & TEXTUREUNIT_STATIC) == 0)
    {
      pass->texanim = texanimlookup[tex[j].texanimid + texOffset];
    }

    rawPasses.push_back(pass);
  }
  g->close();

  std::sort(rawPasses.begin(), rawPasses.end(), &WoWModel::sortPasses);
  passes = rawPasses;
}

bool WoWModel::sortPasses(ModelRenderPass* mrp1, ModelRenderPass* mrp2)
{
  if (mrp1->geoIndex == mrp2->geoIndex)
    return mrp1->specialTex < mrp2->specialTex;
  if (mrp1->blendmode == mrp2->blendmode)
    return (mrp1->geoIndex < mrp2->geoIndex);
  return mrp1->blendmode < mrp2->blendmode;
}

void WoWModel::calcBones(ssize_t anim, size_t time)
{
  // Reset all bones to 'false' which means they haven't been animated yet.
  for (auto & it : bones)
  {
    it.calc = false;
  }

  // Character specific bone animation calculations.
  if (charModelDetails.isChar)
  {

    // Animate the "core" rotations and transformations for the rest of the model to adopt into their transformations
    if (keyBoneLookup[BONE_ROOT] > -1)
    {
      for (int i = 0; i <= keyBoneLookup[BONE_ROOT]; i++)
      {
        bones[i].calcMatrix(bones, anim, time);
      }
    }

    // Find the close hands animation id
    int closeFistID = 0;
    /*
    for (size_t i=0; i<header.nAnimations; i++) {
    if (anims[i].animID==15) {  // closed fist
    closeFistID = i;
    break;
    }
    }
    */
    // Alfred 2009.07.23 use animLookups to speedup
    if (animLookups.size() >= ANIMATION_HANDSCLOSED && animLookups[ANIMATION_HANDSCLOSED] > 0) // closed fist
      closeFistID = animLookups[ANIMATION_HANDSCLOSED];

    // Animate key skeletal bones except the fingers which we do later.
    // -----
    size_t a, t;

    // if we have a "secondary animation" selected,  animate upper body using that.
    if (animManager->GetSecondaryID() > -1)
    {
      a = animManager->GetSecondaryID();
      t = animManager->GetSecondaryFrame();
    }
    else
    {
      a = anim;
      t = time;
    }

    for (size_t i = 0; i < animManager->GetSecondaryCount(); i++)
    { // only goto 5, otherwise it affects the hip/waist rotation for the lower-body.
      if (keyBoneLookup[i] > -1)
        bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
    }

    if (animManager->GetMouthID() > -1)
    {
      // Animate the head and jaw
      if (keyBoneLookup[BONE_HEAD] > -1)
        bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, animManager->GetMouthID(), animManager->GetMouthFrame());
      if (keyBoneLookup[BONE_JAW] > -1)
        bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, animManager->GetMouthID(), animManager->GetMouthFrame());
    }
    else
    {
      // Animate the head and jaw
      if (keyBoneLookup[BONE_HEAD] > -1)
        bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, a, t);
      if (keyBoneLookup[BONE_JAW] > -1)
        bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, a, t);
    }

    // still not sure what 18-26 bone lookups are but I think its more for things like wrist, etc which are not as visually obvious.
    for (size_t i = BONE_BTH; i < BONE_MAX; i++)
    {
      if (keyBoneLookup[i] > -1)
        bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
    }
    // =====

    if (charModelDetails.closeRHand)
    {
      a = closeFistID;
      t = 1;
    }
    else
    {
      a = anim;
      t = time;
    }

    for (size_t i = 0; i < 5; i++)
    {
      if (keyBoneLookup[BONE_RFINGER1 + i] > -1)
        bones[keyBoneLookup[BONE_RFINGER1 + i]].calcMatrix(bones, a, t);
    }

    if (charModelDetails.closeLHand)
    {
      a = closeFistID;
      t = 1;
    }
    else
    {
      a = anim;
      t = time;
    }

    for (size_t i = 0; i < 5; i++)
    {
      if (keyBoneLookup[BONE_LFINGER1 + i] > -1)
        bones[keyBoneLookup[BONE_LFINGER1 + i]].calcMatrix(bones, a, t);
    }
  }
  else
  {
    for (ssize_t i = 0; i < keyBoneLookup[BONE_ROOT]; i++)
    {
      bones[i].calcMatrix(bones, anim, time);
    }

    // The following line fixes 'mounts' in that the character doesn't get rotated, but it also screws up the rotation for the entire model :(
    //bones[18].calcMatrix(bones, anim, time, false);

    // Animate key skeletal bones except the fingers which we do later.
    // -----
    size_t a, t;

    // if we have a "secondary animation" selected,  animate upper body using that.
    if (animManager->GetSecondaryID() > -1)
    {
      a = animManager->GetSecondaryID();
      t = animManager->GetSecondaryFrame();
    }
    else
    {
      a = anim;
      t = time;
    }

    for (size_t i = 0; i < animManager->GetSecondaryCount(); i++)
    { // only goto 5, otherwise it affects the hip/waist rotation for the lower-body.
      if (keyBoneLookup[i] > -1)
        bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
    }

    if (animManager->GetMouthID() > -1)
    {
      // Animate the head and jaw
      if (keyBoneLookup[BONE_HEAD] > -1)
        bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, animManager->GetMouthID(), animManager->GetMouthFrame());
      if (keyBoneLookup[BONE_JAW] > -1)
        bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, animManager->GetMouthID(), animManager->GetMouthFrame());
    }
    else
    {
      // Animate the head and jaw
      if (keyBoneLookup[BONE_HEAD] > -1)
        bones[keyBoneLookup[BONE_HEAD]].calcMatrix(bones, a, t);
      if (keyBoneLookup[BONE_JAW] > -1)
        bones[keyBoneLookup[BONE_JAW]].calcMatrix(bones, a, t);
    }

    // still not sure what 18-26 bone lookups are but I think its more for things like wrist, etc which are not as visually obvious.
    for (size_t i = BONE_ROOT; i < BONE_MAX; i++)
    {
      if (keyBoneLookup[i] > -1)
        bones[keyBoneLookup[i]].calcMatrix(bones, a, t);
    }
  }

  // Animate everything thats left with the 'default' animation
  for (auto & it : bones)
  {
    it.calcMatrix(bones, anim, time);
  }
}

void WoWModel::animate(ssize_t anim)
{
  size_t t = 0;

  ModelAnimation &a = anims[anim];
  int tmax = a.length;
  if (tmax == 0)
    tmax = 1;

  if (isWMO == true)
  {
    t = globalTime;
    t %= tmax;
  }
  else
    t = animManager->GetFrame();

  this->animtime = t;
  this->anim = anim;

  if (animBones) // && (!animManager->IsPaused() || !animManager->IsParticlePaused()))
  {
    calcBones(anim, t);
  }

  if (animGeometry)
  { 
    if (video.supportVBO)
    {
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
      glBufferDataARB(GL_ARRAY_BUFFER_ARB, 2 * vbufsize, NULL, GL_STREAM_DRAW_ARB);

      vertices = (Vec3D*)glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY);

    }

    // transform vertices
    auto ov_it = origVertices.begin();
    for (size_t i = 0; ov_it != origVertices.end(); ++i, ++ov_it)
    { //,k=0
      Vec3D v(0, 0, 0), n(0, 0, 0);

      for (size_t b = 0; b < 4; b++)
      {
        if (ov_it->weights[b] > 0)
        {
          Vec3D tv = bones[ov_it->bones[b]].mat * ov_it->pos;
          Vec3D tn = bones[ov_it->bones[b]].mrot * ov_it->normal;
          v += tv * ((float)ov_it->weights[b] / 255.0f);
          n += tn * ((float)ov_it->weights[b] / 255.0f);
        }
      }

      vertices[i] = v;
      if (video.supportVBO)
        vertices[origVertices.size() + i] = n.normalize(); // shouldn't these be normal by default?
      else
        normals[i] = n;
    }

    // clear bind
    if (video.supportVBO)
    {
      glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
    }
  }

  for (uint i = 0; i < lights.size(); i++)
  {
    if (lights[i].parent >= 0)
    {
      lights[i].tpos = bones[lights[i].parent].mat * lights[i].pos;
      lights[i].tdir = bones[lights[i].parent].mrot * lights[i].dir;
    }
  }

  for (auto & it : particleSystems)
  {
    // random time distribution for teh win ..?
    //int pt = a.timeStart + (t + (int)(tmax*particleSystems[i].tofs)) % tmax;
    it.setup(anim, t);
  }

  for (auto & it : ribbons)
    it.setup(anim, t);

  for (auto & it : texAnims)
    it.calc(anim, t);
}

inline void WoWModel::drawModel()
{
  // assume these client states are enabled: GL_VERTEX_ARRAY, GL_NORMAL_ARRAY, GL_TEXTURE_COORD_ARRAY
  if (video.supportVBO && animated)
  {
    // bind / point to the vertex normals buffer
    if (animGeometry)
    {
      glNormalPointer(GL_FLOAT, 0, GL_BUFFER_OFFSET(vbufsize));
    }
    else
    {
      glBindBufferARB(GL_ARRAY_BUFFER_ARB, nbuf);
      glNormalPointer(GL_FLOAT, 0, 0);
    }

    // Bind the vertex buffer
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
    glVertexPointer(3, GL_FLOAT, 0, 0);
    // Bind the texture coordinates buffer
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, tbuf);
    glTexCoordPointer(2, GL_FLOAT, 0, 0);

  }
  else if (animated)
  {
    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glNormalPointer(GL_FLOAT, 0, normals);
    glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
  }

  // Display in wireframe mode?
  if (showWireframe)
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  // Render the various parts of the model.
  for (auto it : passes)
  {
    if (it->init())
    {
      it->render(animated);
      it->deinit();
    }
  }
  
  if (showWireframe)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // clean bind
  if (video.supportVBO && animated)
  {
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
  }

  // done with all render ops
}

inline void WoWModel::draw()
{
  if (!ok)
    return;

  if (!animated)
  {
    if (showModel)
      glCallList(dlist);

  }
  else
  {
    if (ind)
    {
      animate(currentAnim);
    }
    else
    {
      if (!animcalc)
      {
        animate(currentAnim);
        //animcalc = true; // Not sure what this is really for but it breaks WMO animation
      }
    }

    if (showModel)
      drawModel();
  }
}

// These aren't really needed in the model viewer.. only wowmapviewer
void WoWModel::lightsOn(GLuint lbase)
{
  // setup lights
  for (uint i = 0, l = lbase; i < lights.size(); i++)
    lights[i].setup(animtime, (GLuint)l++);
}

// These aren't really needed in the model viewer.. only wowmapviewer
void WoWModel::lightsOff(GLuint lbase)
{
  for (uint i = 0, l = lbase; i < lights.size(); i++)
    glDisable((GLenum)l++);
}

// Updates our particles within models.
void WoWModel::updateEmitters(float dt)
{
  if (!ok || !showParticles || !GLOBALSETTINGS.bShowParticle)
    return;

  for (auto & it : particleSystems)
  {
    it.update(dt);
    it.replaceParticleColors = replaceParticleColors;
    it.particleColorReplacements = particleColorReplacements;
  }
}


// Draws the "bones" of models  (skeletal animation)
void WoWModel::drawBones()
{
  glDisable(GL_DEPTH_TEST);
  glBegin(GL_LINES);
  for (auto it : bones)
  {
    //for (size_t i=30; i<40; i++) {
    if (it.parent != -1)
    {
      glVertex3fv(it.transPivot);
      glVertex3fv(bones[it.parent].transPivot);
    }
  }
  glEnd();
  glEnable(GL_DEPTH_TEST);
}

// Sets up the models attachments
void WoWModel::setupAtt(int id)
{
  int l = attLookup[id];
  if (l > -1)
    atts[l].setup();
}

// Sets up the models attachments
void WoWModel::setupAtt2(int id)
{
  int l = attLookup[id];
  if (l >= 0)
    atts[l].setupParticle();
}

// Draws the Bounding Volume, which is used for Collision detection.
void WoWModel::drawBoundingVolume()
{
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glBegin(GL_TRIANGLES);
  for (uint i = 0; i < boundTris.size(); i++)
  {
    size_t v = boundTris[i];
    if (v < bounds.size())
      glVertex3fv(bounds[v]);
    else
      glVertex3f(0, 0, 0);
  }
  glEnd();
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Renders our particles into the pipeline.
void WoWModel::drawParticles()
{
  // draw particle systems
  for (auto & it : particleSystems)
    it.draw();

  // draw ribbons
  for (auto & it : ribbons)
    it.draw();
}

WoWItem * WoWModel::getItem(CharSlots slot)
{

  for (WoWModel::iterator it = this->begin();
       it != this->end();
       ++it)
  {
    if ((*it)->slot() == slot)
      return *it;
  }

  return 0;
}

void WoWModel::update(int dt) // (float dt)
{
  if (animated && animManager != NULL)
    animManager->Tick(dt);
  updateEmitters((dt/1000.0f));
}

void WoWModel::updateTextureList(GameFile * tex, int special)
{
  for (size_t i = 0; i < specialTextures.size(); i++)
  {
    if (specialTextures[i] == special)
    {
      if (replaceTextures[special] != ModelRenderPass::INVALID_TEX)
        TEXTUREMANAGER.del(replaceTextures[special]);

      replaceTextures[special] = TEXTUREMANAGER.add(tex);
      break;
    }
  }
}

std::map<int, std::wstring> WoWModel::getAnimsMap()
{
  std::map<int, std::wstring> result;
  if (animated && anims.size() > 0)
  {
    QString query = "SELECT ID,NAME FROM AnimationData WHERE ID IN(";
    for (unsigned int i = 0; i < anims.size(); i++)
    {
      query += QString::number(anims[i].animID);
      if (i < anims.size() - 1)
        query += ",";
      else
        query += ")";
    }

    sqlResult animsResult = GAMEDATABASE.sqlQuery(query);

    if (animsResult.valid && !animsResult.empty())
    {
      LOG_INFO << "Found" << animsResult.values.size() << "animations for model";

      // remap database results on model header indexes
      for (int i = 0, imax = animsResult.values.size(); i < imax; i++)
      {
        result[animsResult.values[i][0].toInt()] = animsResult.values[i][1].toStdWString();
      }
    }
  }
  return result;
}

void WoWModel::save(QXmlStreamWriter &stream)
{
  stream.writeStartElement("model");
  stream.writeStartElement("file");
  stream.writeAttribute("name", QString::fromStdString(modelname));
  stream.writeEndElement();
  cd.save(stream);
  stream.writeEndElement(); // model
}

void WoWModel::load(QString &file)
{
  cd.load(file);
}

bool WoWModel::canSetTextureFromFile(int texnum)
{
  for (size_t i = 0; i < TEXTURE_MAX; i++)
  {
    if (specialTextures[i] == texnum)
      return 1;
  }
  return 0;
}

void WoWModel::computeMinMaxCoords(Vec3D & minCoord, Vec3D & maxCoord)
{
  if (video.supportVBO)
  {
    // get back vertices
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
    vertices = (Vec3D*)glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_READ_ONLY);
  }

  for (auto & it : passes)
  {
    ModelGeosetHD * geoset = geosets[it->geoIndex];
    if (!geoset->display)
      continue;

    for (size_t k = 0, b = geoset->istart; k < geoset->icount; k++, b++)
    {
      Vec3D v = vertices[indices[b]];

      // detect min/max coordinates and set them
      if (v.x < minCoord.x)
        minCoord.x = v.x;
      else if (v.x > maxCoord.x)
        maxCoord.x = v.x;

      if (v.y < minCoord.y)
        minCoord.y = v.y;
      else if (v.y > maxCoord.y)
        maxCoord.y = v.y;

      if (v.z < minCoord.z)
        minCoord.z = v.z;
      else if (v.z > maxCoord.z)
        maxCoord.z = v.z;
    }
  }

  if (video.supportVBO)
  {
    glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
    vertices = 0;
  }

  LOG_INFO << __FUNCTION__;
  LOG_INFO << "min" << minCoord.x << minCoord.y << minCoord.z;
  LOG_INFO << "max" << maxCoord.x << maxCoord.y << maxCoord.z;
}

QString WoWModel::getCGGroupName(CharGeosets cg)
{
  QString result = "";

  static std::map<CharGeosets, QString> groups =
  { { CG_HAIRSTYLE, "Main" }, { CG_GEOSET100, "Facial1" }, { CG_GEOSET200, "Facial2" }, { CG_GEOSET300, "Facial3" },
  { CG_GLOVES, "Bracers" }, { CG_BOOTS, "Boots" }, { CG_EARS, "Ears" }, { CG_WRISTBANDS, "Wristbands" },
  { CG_KNEEPADS, "Kneepads" }, { CG_PANTS, "Pants" }, { CG_PANTS2, "Pants2" }, { CG_TARBARD, "Tabard" },
  { CG_TROUSERS, "Trousers" }, { CG_TARBARD2, "Tabard2" }, { CG_CAPE, "Cape" }, { CG_EYEGLOW, "Eyeglows" },
  { CG_BELT, "Belt" }, { CG_TAIL, "Tail" }, { CG_HDFEET, "Feet" }, { CG_HANDS, "Hands" },
  { CG_DH_HORNS, "Horns" }, { CG_DH_BLINDFOLDS, "BlindFolds" } };

  auto it = groups.find(cg);
  if (it != groups.end())
    result = it->second;

  return result;
}

void WoWModel::showGeoset(uint geosetindex, bool value)
{
  if (geosetindex < geosets.size())
    geosets[geosetindex]->display = value;
}

bool WoWModel::isGeosetDisplayed(uint geosetindex)
{
  bool result = false;

  if (geosetindex < geosets.size())
    result = geosets[geosetindex]->display;

  return result;
}

void WoWModel::setGeosetGroupDisplay(CharGeosets group, int val)
{
  int a = (int)group * 100;
  int b = ((int)group + 1) * 100;
  int geosetID = a + val;

  // This loop must be done only on first geosets (original ones) in case of merged models
  // This is why rawGeosets.size() is used as a stop criteria even if we are looping over
  // geosets member
  for (uint i = 0; i < rawGeosets.size(); i++)
  {
    int id = geosets[i]->id;
    if (id > a && id < b)
      showGeoset(i, (id == geosetID));
  }
}

void WoWModel::setCreatureGeosetData(std::set<GeosetNum> cgd)
{
  // Hide geosets that were set by old creatureGeosetData:
  if (creatureGeosetData.size() > 0)
    restoreRawGeosets();

  creatureGeosetData = cgd;
  if (!cgd.size())
    return;
  int geomax = *cgd.rbegin() + 1; // highest value in set

  // We should only be dealing with geosets below 900, but just in case
  // Blizzard changes this, we'll set the max higher and log that it's happened:
  if (geomax > 900)
  {
    LOG_ERROR << "setCreatureGeosetData value of " << geomax <<
                 " detected. We were assuming the maximum was 899.";
    geomax = ((geomax/100)+1)*100;  // round the max up to the next 100 (next geoset group)
  }
  else
    geomax = 900;
  // If creatureGeosetData is used then we switch off
  // ALL geosets from 1 to 899 that aren't specified by it:
  for (uint i = 0; i < rawGeosets.size(); i++)
  {
    int id = geosets[i]->id;
    if (id > 0 && id < geomax)
      showGeoset(i, cgd.count(id) > 0);
  }
}


void WoWModel::mergeModel(QString & name)
{
  LOG_INFO << __FUNCTION__ << name;
  if(mergedModels.end() != std::find_if(std::begin(mergedModels),
                                        std::end(mergedModels),
                                        [&](const WoWModel * m){ return m->gamefile->fullname() == name.replace("\\", "/"); }))
    return;

  WoWModel * m = new WoWModel(GAMEDIRECTORY.getFile(name), true);

  if (!m->ok)
    return;

  mergeModel(m);
}

void WoWModel::mergeModel(WoWModel * m)
{
  LOG_INFO << __FUNCTION__ << m;
  auto it = mergedModels.insert(m);
  if (it.second == true) // new element inserted
    refreshMerging();
}
  
void WoWModel::refreshMerging()
{
  LOG_INFO << __FUNCTION__;
  for (auto it : mergedModels)
    LOG_INFO << it->name() << it->gamefile->fullname();

  // first reinit this model with original data
  origVertices = rawVertices;
  indices = rawIndices;
  passes = rawPasses;
  geosets = rawGeosets;
  textures.resize(TEXTURE_MAX);
  replaceTextures.resize(TEXTURE_MAX);
  specialTextures.resize(TEXTURE_MAX);

  uint mergeIndex = 0;
  for (auto modelsIt : mergedModels)
  {
    uint nbVertices = origVertices.size();
    uint nbIndices = indices.size();
    uint nbGeosets = geosets.size();

    // reinit merged model as well, just in case
    modelsIt->origVertices = modelsIt->rawVertices;
    modelsIt->indices = modelsIt->rawIndices;
    modelsIt->passes = modelsIt->rawPasses;
    modelsIt->restoreRawGeosets();

    mergeIndex++;

    for (auto it : modelsIt->geosets)
    {
      it->istart += nbIndices;
      it->vstart += nbVertices;
      geosets.push_back(it);
    }

    // build bone corresponsance table
    uint32 nbBonesInNewModel = modelsIt->header.nBones;
    int16 * boneConvertTable = new int16[nbBonesInNewModel];

    for (uint i = 0; i < nbBonesInNewModel; ++i)
      boneConvertTable[i] = i;

    for (uint i = 0; i < nbBonesInNewModel; ++i)
    {
      Vec3D pivot = modelsIt->bones[i].pivot;
      for (uint b = 0; b < bones.size(); ++b)
      {
        Vec3D p = bones[b].pivot;
        if ((p == pivot) && 
            (bones[b].boneDef.unknown == modelsIt->bones[i].boneDef.unknown))
        {
          boneConvertTable[i] = b;
          break;
        }
      }
    }

#ifdef DEBUG_DH_SUPPORT
    for (uint i = 0; i < nbBonesInNewModel; ++i)
      LOG_INFO << i << "=>" << boneConvertTable[i];
#endif

    // change bone from new model to character one
    for (auto & it : modelsIt->origVertices)
    {
      for (uint i = 0; i < 4; ++i)
      {
        if (it.weights[i] > 0)
          it.bones[i] = boneConvertTable[it.bones[i]];
      }
    }

    delete[] boneConvertTable;

    origVertices.reserve(origVertices.size() + modelsIt->origVertices.size());
    origVertices.insert(origVertices.end(), modelsIt->origVertices.begin(), modelsIt->origVertices.end());

    indices.reserve(indices.size() + modelsIt->indices.size());

    for (auto & it : modelsIt->indices)
      indices.push_back(it + nbVertices);

    // retrieve tex id associated to model hands (needed for DH)
    uint16 handTex = ModelRenderPass::INVALID_TEX;
    for (auto it : passes)
    {
      if (geosets[it->geoIndex]->id / 100 == 23)
        handTex = it->tex;
    }

    for (auto it : modelsIt->passes)
    {
      ModelRenderPass * p = new ModelRenderPass(*it);
      p->model = this;
      p->geoIndex += nbGeosets;
      if (geosets[it->geoIndex]->id / 100 != 23) // don't copy texture for hands
        p->tex += (mergeIndex * TEXTURE_MAX);
      else
        p->tex = handTex; // use regular model texture instead

      passes.push_back(p);
    }

#ifdef DEBUG_DH_SUPPORT
    LOG_INFO << "---- FINAL ----";
    LOG_INFO << "nbGeosets =" << geosets.size();
    LOG_INFO << "nbVertices =" << origVertices.size();
    LOG_INFO << "nbIndices =" << indices.size();
    LOG_INFO << "nbPasses =" << passes.size();
#endif

    // add model textures
    for (auto it : modelsIt->textures)
    {
      if (it != ModelRenderPass::INVALID_TEX)
        TEXTUREMANAGER.add(GAMEDIRECTORY.getFile(TEXTUREMANAGER.get(it)));
      textures.push_back(it);
    }

    uint tmax = specialTextures.size();
    for (auto it : modelsIt->specialTextures)
    {
      int val = it;
      if (it != -1)
        val += tmax;

      specialTextures.push_back(val);
    }

    for (auto it : modelsIt->replaceTextures)
      replaceTextures.push_back(it);
  }

  delete[] vertices;
  delete[] normals;

  vertices = new Vec3D[origVertices.size()];
  normals = new Vec3D[origVertices.size()];

  uint i = 0;
  for (auto & ov_it : origVertices)
  {
    // Set the data for our vertices, normals from the model data
    vertices[i] = ov_it.pos;
    normals[i] = ov_it.normal.normalize();
    ++i;
  }

  const size_t size = (origVertices.size() * sizeof(float));
  vbufsize = (3 * size); // we multiple by 3 for the x, y, z positions of the vertex

  if (video.supportVBO)
  {
    glDeleteBuffersARB(1, &nbuf);
    glDeleteBuffersARB(1, &vbuf);

    // Vert buffer
    glGenBuffersARB(1, &vbuf);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbuf);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, vertices, GL_STATIC_DRAW_ARB);
    delete[] vertices; vertices = 0;

    // normals buffer
    glGenBuffersARB(1, &nbuf);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, nbuf);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, vbufsize, normals, GL_STATIC_DRAW_ARB);
    delete[] normals; normals = 0;

    // clean bind
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
  }

  refresh();
}

void WoWModel::unmergeModel(QString & name)
{
  LOG_INFO << __FUNCTION__ << name;
  auto it = std::find_if(std::begin(mergedModels),
                         std::end(mergedModels),
                         [&](const WoWModel * m){ return m->gamefile->fullname() == name.replace("\\", "/"); });

  if (it != mergedModels.end())
  {
    WoWModel * m = *it;
    unmergeModel(m);
    delete m;
  }
}

void WoWModel::unmergeModel(WoWModel * m)
{
  LOG_INFO << __FUNCTION__ << m->name();
  mergedModels.erase(m);
  refreshMerging();
}


void WoWModel::refresh()
{
  TextureID charTex = 0;
  bool showScalp = true;

  // Reset geosets
  for (size_t i = 0; i < NUM_GEOSETS; i++)
    cd.geosets[i] = 1;

  RaceInfos infos;
  if (!RaceInfos::getCurrent(this, infos)) // if no race info found, simply update geosets
  {
    // reset geosets
    for (uint i = 0; i < NUM_GEOSETS; i++)
      setGeosetGroupDisplay((CharGeosets)i, cd.geosets[i]);

    return;
  }
   
  cd.geosets[CG_GEOSET100] = cd.geosets[CG_GEOSET200] = cd.geosets[CG_GEOSET300] = 0;

  // show ears, if toggled
  if (cd.showEars)
    cd.geosets[CG_EARS] = 2;

  tex.reset(infos.textureLayoutID);

  std::vector<int> foundTextures = cd.getTextureForSection(CharDetails::SkinType);

  if (foundTextures.size() > 0)
    tex.setBaseImage(GAMEDIRECTORY.getFile(foundTextures[0]));

  if (foundTextures.size() > 1)
  {
    GameFile * tex = GAMEDIRECTORY.getFile(foundTextures[1]);
    updateTextureList(tex, TEXTURE_FUR);
  }

  // Display underwear on the model?
  if (cd.showUnderwear)
  {
    foundTextures = cd.getTextureForSection(CharDetails::UnderwearType);
    if (foundTextures.size() > 0)
      tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[0]), CR_LEG_UPPER, 1); // pants

    if (foundTextures.size() > 1)
      tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[1]), CR_TORSO_UPPER, 1); // top

    if ((infos.raceid == RACE_PANDAREN) && (infos.sexid == GENDER_FEMALE))
      cd.geosets[CG_TARBARD2] = 1;
  }
  else
  {
    if ((infos.raceid == RACE_PANDAREN) && (infos.sexid == GENDER_FEMALE))
      cd.geosets[CG_TARBARD2] = 0;
  }

  // face
  foundTextures = cd.getTextureForSection(CharDetails::FaceType);
  if (foundTextures.size() > 0)
    tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[0]), CR_FACE_LOWER, 1);

  if (foundTextures.size() > 1)
    tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[1]), CR_FACE_UPPER, 1);

  // facial hair
  foundTextures = cd.getTextureForSection(CharDetails::FacialHairType);
  if (foundTextures.size() > 0)
    tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[0]), CR_FACE_LOWER, 2);

  if (foundTextures.size() > 1)
    tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[1]), CR_FACE_UPPER, 2);

  // select hairstyle geoset(s)
  QString query = QString("SELECT GeoSetID,ShowScalp FROM CharHairGeoSets WHERE RaceID=%1 AND SexID=%2 AND VariationID=%3")
    .arg(infos.raceid)
    .arg(infos.sexid)
    .arg(cd.get(CharDetails::FACIAL_CUSTOMIZATION_STYLE));
  sqlResult hairStyle = GAMEDATABASE.sqlQuery(query);

  LOG_INFO << query;

  if (hairStyle.valid && !hairStyle.values.empty())
  {
    showScalp = (bool)hairStyle.values[0][1].toInt();
    unsigned int geosetId = hairStyle.values[0][0].toInt();
    if (!geosetId)  // adds missing scalp if no other hair geoset used. Seems to work that way, anyway...
      geosetId = 1;
    cd.geosets[CG_HAIRSTYLE] = geosetId;
    /*
    for (size_t j = 0; j < rawGeosets.size(); j++)
    {
      int id = geosets[j]->id;
      if (!id) // 0 is for skin, not hairstyle
        continue;
      if (id == geosetId)
        showGeoset(j, cd.showHair);
      else if (id < 100)
        showGeoset(j, false);
    }
    */
  }
  else
    LOG_ERROR << "Unable to collect hair style " << cd.get(CharDetails::FACIAL_CUSTOMIZATION_STYLE) << " for model " << name();


  // Hair texture
  foundTextures = cd.getTextureForSection(CharDetails::HairType);
  if (foundTextures.size() > 0)
  {
    GameFile * texture = GAMEDIRECTORY.getFile(foundTextures[0]);
    updateTextureList(texture, TEXTURE_HAIR);

    if (infos.isHD)
    {
      if (!showScalp && foundTextures.size() > 1)
        tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[1]), CR_FACE_UPPER, 3);
    }
    else
    {
      if (!showScalp)
      {
        if (foundTextures.size() > 1)
          tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[1]), CR_FACE_LOWER, 3);

        if (foundTextures.size() > 2)
          tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[2]), CR_FACE_UPPER, 3);
      }
    }
  }

  // select facial geoset(s)
  query = QString("SELECT GeoSet1,GeoSet2,GeoSet3,GeoSet4,GeoSet5 FROM CharacterFacialHairStyles WHERE RaceID=%1 AND SexID=%2 AND VariationID=%3")
    .arg(infos.raceid)
    .arg(infos.sexid)
    .arg(cd.get(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION));

  sqlResult facialHairStyle = GAMEDATABASE.sqlQuery(query);

  if (facialHairStyle.valid && !facialHairStyle.values.empty() && cd.showFacialHair)
  {
    LOG_INFO << "Facial GeoSets : " << facialHairStyle.values[0][0].toInt()
      << " " << facialHairStyle.values[0][1].toInt()
      << " " << facialHairStyle.values[0][2].toInt()
      << " " << facialHairStyle.values[0][3].toInt()
      << " " << facialHairStyle.values[0][4].toInt();

    cd.geosets[CG_GEOSET100] = facialHairStyle.values[0][0].toInt();
    cd.geosets[CG_GEOSET200] = facialHairStyle.values[0][2].toInt();
    cd.geosets[CG_GEOSET300] = facialHairStyle.values[0][1].toInt();
  }
  else
  {
    LOG_ERROR << "Unable to collect number of facial hair style" << cd.get(CharDetails::ADDITIONAL_FACIAL_CUSTOMIZATION) << "for model" << name();
  }

  // DH customization
  // tattoos
  foundTextures = cd.getTextureForSection(CharDetails::TattooType);
  if (foundTextures.size() > 0)
    tex.addLayer(GAMEDIRECTORY.getFile(foundTextures[0]), CR_DH_TATTOOS, 1);

  // horns
  cd.geosets[CG_DH_HORNS] = cd.get(CharDetails::DH_HORN_STYLE);

  // blindfolds
  cd.geosets[CG_DH_BLINDFOLDS] = cd.get(CharDetails::DH_BLINDFOLDS);

  //refresh equipment

  for (WoWModel::iterator it = begin(); it != end(); ++it)
    (*it)->refresh();

  LOG_INFO << "Current Equipment :"
    << "Head" << getItem(CS_HEAD)->id()
    << "Shoulder" << getItem(CS_SHOULDER)->id()
    << "Shirt" << getItem(CS_SHIRT)->id()
    << "Chest" << getItem(CS_CHEST)->id()
    << "Belt" << getItem(CS_BELT)->id()
    << "Legs" << getItem(CS_PANTS)->id()
    << "Boots" << getItem(CS_BOOTS)->id()
    << "Bracers" << getItem(CS_BRACERS)->id()
    << "Gloves" << getItem(CS_GLOVES)->id()
    << "Cape" << getItem(CS_CAPE)->id()
    << "Right Hand" << getItem(CS_HAND_RIGHT)->id()
    << "Left Hand" << getItem(CS_HAND_LEFT)->id()
    << "Quiver" << getItem(CS_QUIVER)->id()
    << "Tabard" << getItem(CS_TABARD)->id();

  // gloves - this is so gloves have preference over shirt sleeves.
  if (cd.geosets[CG_GLOVES] > 1)
    cd.geosets[CG_WRISTBANDS] = 0;

  // pandaren female -> hide tabard geoset if TROUSERS geosets are displayed
  if ((infos.raceid == RACE_PANDAREN) && (infos.sexid == GENDER_FEMALE) 
      && (cd.geosets[CG_TROUSERS] > 1))
    cd.geosets[CG_TARBARD2] = 0;

  // reset geosets
  for (uint i = 0; i < NUM_GEOSETS; i++)
    setGeosetGroupDisplay((CharGeosets)i, cd.geosets[i]);

  // refresh DH geosets
  for (auto it : mergedModels)
  {
    if (it->name().contains("demonhuntergeosets"))
    {
      it->setGeosetGroupDisplay(CG_DH_HORNS, cd.geosets[CG_DH_HORNS]);
      it->setGeosetGroupDisplay(CG_DH_BLINDFOLDS, cd.geosets[CG_DH_BLINDFOLDS]);
      break;
    }
  }

  

  WoWItem * headItem = getItem(CS_HEAD);

  if (headItem != 0 && headItem->id() != -1 && cd.autoHideGeosetsForHeadItems)
  {
    QString query = QString("SELECT GeoSetGroup FROM HelmetGeosetData WHERE HelmetGeosetData.RaceID = %1 " 
                            "AND HelmetGeosetData.GeosetVisDataID = (SELECT %2 FROM ItemDisplayInfo WHERE ItemDisplayInfo.ID = "
                            "(SELECT ItemDisplayInfoID FROM ItemAppearance WHERE ID = (SELECT ItemAppearanceID FROM ItemModifiedAppearance WHERE ItemID = %3)))")
                            .arg(infos.raceid)
                            .arg((infos.sexid == 0) ? "HelmetGeosetVis1" : "HelmetGeosetVis2")
                            .arg(headItem->id());



    sqlResult helmetInfos = GAMEDATABASE.sqlQuery(query);

    if (helmetInfos.valid && !helmetInfos.values.empty())
    {
      for (auto it : helmetInfos.values)
      {
        setGeosetGroupDisplay((CharGeosets)it[0].toInt(), 0);
      }
    }
  }

  // finalize character texture
  tex.compose(charTex);

  // set replacable textures
  replaceTextures[TEXTURE_BODY] = charTex;

  // If model is one of these races, show the feet (don't wear boots)
  cd.showFeet = infos.barefeet;

  // Eye Glow Geosets are ID 1701, 1702, etc.
  size_t egt = cd.eyeGlowType;
  int egtId = CG_EYEGLOW * 100 + egt + 1;   // CG_EYEGLOW = 17
  for (size_t i = 0; i < rawGeosets.size(); i++)
  {
    int id = geosets[i]->id;
    if ((int)(id / 100) == CG_EYEGLOW)  // geosets 1700..1799
      showGeoset(i, (id == egtId));
  }

  // Quick & Dirty fix for gobelins => deactivate buggy geosets
  if (infos.raceid == 9)
  {
    if (infos.sexid == 0)
    {
      showGeoset(1, false);
      showGeoset(6, false);
    }
    else
    {
      showGeoset(0, false);
      showGeoset(3, false);
    }
  }
}

QString WoWModel::getNameForTex(uint16 tex)
{
  if (specialTextures[tex] == TEXTURE_BODY)
    return "Body.blp";
  else
    return TEXTUREMANAGER.get(getGLTexture(tex));
}

GLuint WoWModel::getGLTexture(uint16 tex) const
{
  if (tex >= specialTextures.size())
    return ModelRenderPass::INVALID_TEX;

  if (specialTextures[tex] == -1)
  {
    return textures[tex];
  }
  else
  {
    if (specialTextures[tex] == -1)
      return ModelRenderPass::INVALID_TEX;
    else
      return replaceTextures[specialTextures[tex]];
  }
    
}

void WoWModel::restoreRawGeosets()
{
  std::vector<bool> geosetDisplayStatus;

  for (auto it : geosets)
  {
    geosetDisplayStatus.push_back(it->display);
    delete it;
  }

  geosets.clear();
 
  for (auto it : rawGeosets)
  {
    ModelGeosetHD * geo = new ModelGeosetHD(*it);
    geosets.push_back(geo);
  }

  uint i = 0;
  for (auto it : geosetDisplayStatus)
  {
    geosets[i]->display = it;
    i++;
  }
}


std::ostream& operator<<(std::ostream& out, const WoWModel& m)
{
  out << "<m2>" << endl;
  out << "  <info>" << endl;
  out << "    <modelname>" << m.modelname.c_str() << "</modelname>" << endl;
  out << "  </info>" << endl;
  out << "  <header>" << endl;
  //	out << "    <id>" << m.header.id << "</id>" << endl;
  out << "    <nameLength>" << m.header.nameLength << "</nameLength>" << endl;
  out << "    <nameOfs>" << m.header.nameOfs << "</nameOfs>" << endl;
  //	out << "    <name>" << f.getBuffer()+m.header.nameOfs << "</name>" << endl; // @TODO
  out << "    <GlobalModelFlags>" << m.header.GlobalModelFlags << "</GlobalModelFlags>" << endl;
  out << "    <nGlobalSequences>" << m.header.nGlobalSequences << "</nGlobalSequences>" << endl;
  out << "    <ofsGlobalSequences>" << m.header.ofsGlobalSequences << "</ofsGlobalSequences>" << endl;
  out << "    <nAnimations>" << m.header.nAnimations << "</nAnimations>" << endl;
  out << "    <ofsAnimations>" << m.header.ofsAnimations << "</ofsAnimations>" << endl;
  out << "    <nAnimationLookup>" << m.header.nAnimationLookup << "</nAnimationLookup>" << endl;
  out << "    <ofsAnimationLookup>" << m.header.ofsAnimationLookup << "</ofsAnimationLookup>" << endl;
  out << "    <nBones>" << m.header.nBones << "</nBones>" << endl;
  out << "    <ofsBones>" << m.header.ofsBones << "</ofsBones>" << endl;
  out << "    <nKeyBoneLookup>" << m.header.nKeyBoneLookup << "</nKeyBoneLookup>" << endl;
  out << "    <ofsKeyBoneLookup>" << m.header.ofsKeyBoneLookup << "</ofsKeyBoneLookup>" << endl;
  out << "    <nVertices>" << m.header.nVertices << "</nVertices>" << endl;
  out << "    <ofsVertices>" << m.header.ofsVertices << "</ofsVertices>" << endl;
  out << "    <nViews>" << m.header.nViews << "</nViews>" << endl;
  out << "    <lodname>" << m.lodname.c_str() << "</lodname>" << endl;
  out << "    <nColors>" << m.header.nColors << "</nColors>" << endl;
  out << "    <ofsColors>" << m.header.ofsColors << "</ofsColors>" << endl;
  out << "    <nTextures>" << m.header.nTextures << "</nTextures>" << endl;
  out << "    <ofsTextures>" << m.header.ofsTextures << "</ofsTextures>" << endl;
  out << "    <nTransparency>" << m.header.nTransparency << "</nTransparency>" << endl;
  out << "    <ofsTransparency>" << m.header.ofsTransparency << "</ofsTransparency>" << endl;
  out << "    <nTexAnims>" << m.header.nTexAnims << "</nTexAnims>" << endl;
  out << "    <ofsTexAnims>" << m.header.ofsTexAnims << "</ofsTexAnims>" << endl;
  out << "    <nTexReplace>" << m.header.nTexReplace << "</nTexReplace>" << endl;
  out << "    <ofsTexReplace>" << m.header.ofsTexReplace << "</ofsTexReplace>" << endl;
  out << "    <nTexFlags>" << m.header.nTexFlags << "</nTexFlags>" << endl;
  out << "    <ofsTexFlags>" << m.header.ofsTexFlags << "</ofsTexFlags>" << endl;
  out << "    <nBoneLookup>" << m.header.nBoneLookup << "</nBoneLookup>" << endl;
  out << "    <ofsBoneLookup>" << m.header.ofsBoneLookup << "</ofsBoneLookup>" << endl;
  out << "    <nTexLookup>" << m.header.nTexLookup << "</nTexLookup>" << endl;
  out << "    <ofsTexLookup>" << m.header.ofsTexLookup << "</ofsTexLookup>" << endl;
  out << "    <nTexUnitLookup>" << m.header.nTexUnitLookup << "</nTexUnitLookup>" << endl;
  out << "    <ofsTexUnitLookup>" << m.header.ofsTexUnitLookup << "</ofsTexUnitLookup>" << endl;
  out << "    <nTransparencyLookup>" << m.header.nTransparencyLookup << "</nTransparencyLookup>" << endl;
  out << "    <ofsTransparencyLookup>" << m.header.ofsTransparencyLookup << "</ofsTransparencyLookup>" << endl;
  out << "    <nTexAnimLookup>" << m.header.nTexAnimLookup << "</nTexAnimLookup>" << endl;
  out << "    <ofsTexAnimLookup>" << m.header.ofsTexAnimLookup << "</ofsTexAnimLookup>" << endl;
  out << "    <collisionSphere>" << endl;
  out << "      <min>" << m.header.collisionSphere.min << "</min>" << endl;
  out << "      <max>" << m.header.collisionSphere.max << "</max>" << endl;
  out << "      <radius>" << m.header.collisionSphere.radius << "</radius>" << endl;
  out << "    </collisionSphere>" << endl;
  out << "    <boundSphere>" << endl;
  out << "      <min>" << m.header.boundSphere.min << "</min>" << endl;
  out << "      <max>" << m.header.boundSphere.max << "</max>" << endl;
  out << "      <radius>" << m.header.boundSphere.radius << "</radius>" << endl;
  out << "    </boundSphere>" << endl;
  out << "    <nBoundingTriangles>" << m.header.nBoundingTriangles << "</nBoundingTriangles>" << endl;
  out << "    <ofsBoundingTriangles>" << m.header.ofsBoundingTriangles << "</ofsBoundingTriangles>" << endl;
  out << "    <nBoundingVertices>" << m.header.nBoundingVertices << "</nBoundingVertices>" << endl;
  out << "    <ofsBoundingVertices>" << m.header.ofsBoundingVertices << "</ofsBoundingVertices>" << endl;
  out << "    <nBoundingNormals>" << m.header.nBoundingNormals << "</nBoundingNormals>" << endl;
  out << "    <ofsBoundingNormals>" << m.header.ofsBoundingNormals << "</ofsBoundingNormals>" << endl;
  out << "    <nAttachments>" << m.header.nAttachments << "</nAttachments>" << endl;
  out << "    <ofsAttachments>" << m.header.ofsAttachments << "</ofsAttachments>" << endl;
  out << "    <nAttachLookup>" << m.header.nAttachLookup << "</nAttachLookup>" << endl;
  out << "    <ofsAttachLookup>" << m.header.ofsAttachLookup << "</ofsAttachLookup>" << endl;
  out << "    <nEvents>" << m.header.nEvents << "</nEvents>" << endl;
  out << "    <ofsEvents>" << m.header.ofsEvents << "</ofsEvents>" << endl;
  out << "    <nLights>" << m.header.nLights << "</nLights>" << endl;
  out << "    <ofsLights>" << m.header.ofsLights << "</ofsLights>" << endl;
  out << "    <nCameras>" << m.header.nCameras << "</nCameras>" << endl;
  out << "    <ofsCameras>" << m.header.ofsCameras << "</ofsCameras>" << endl;
  out << "    <nCameraLookup>" << m.header.nCameraLookup << "</nCameraLookup>" << endl;
  out << "    <ofsCameraLookup>" << m.header.ofsCameraLookup << "</ofsCameraLookup>" << endl;
  out << "    <nRibbonEmitters>" << m.header.nRibbonEmitters << "</nRibbonEmitters>" << endl;
  out << "    <ofsRibbonEmitters>" << m.header.ofsRibbonEmitters << "</ofsRibbonEmitters>" << endl;
  out << "    <nParticleEmitters>" << m.header.nParticleEmitters << "</nParticleEmitters>" << endl;
  out << "    <ofsParticleEmitters>" << m.header.ofsParticleEmitters << "</ofsParticleEmitters>" << endl;
  out << "  </header>" << endl;

  out << "  <SkeletonAndAnimation>" << endl;

  out << "  <GlobalSequences size=\"" << m.globalSequences.size() << "\">" << endl;
  for (size_t i = 0; i < m.globalSequences.size(); i++)
    out << "<Sequence>" << m.globalSequences[i] << "</Sequence>" << endl;
  out << "  </GlobalSequences>" << endl;

  out << "  <Animations size=\"" << m.anims.size() << "\">" << endl;
  for (size_t i = 0; i < m.anims.size(); i++)
  {
    out << "    <Animation id=\"" << i << "\">" << endl;
    out << "      <animID>" << m.anims[i].animID << "</animID>" << endl;
    string strName;
    QString query = QString("SELECT Name FROM AnimationData WHERE ID = %1").arg(m.anims[i].animID);
    sqlResult anim = GAMEDATABASE.sqlQuery(query);
    if (anim.valid && !anim.empty())
      strName = anim.values[0][0].toStdString();
    else
      strName = "???";
    out << "      <animName>" << strName << "</animName>" << endl;
    out << "      <length>" << m.anims[i].length << "</length>" << endl;
    out << "      <moveSpeed>" << m.anims[i].moveSpeed << "</moveSpeed>" << endl;
    out << "      <flags>" << m.anims[i].flags << "</flags>" << endl;
    out << "      <probability>" << m.anims[i].probability << "</probability>" << endl;
    out << "      <d1>" << m.anims[i].d1 << "</d1>" << endl;
    out << "      <d2>" << m.anims[i].d2 << "</d2>" << endl;
    out << "      <playSpeed>" << m.anims[i].playSpeed << "</playSpeed>" << endl;
    out << "      <boxA>" << m.anims[i].boundSphere.min << "</boxA>" << endl;
    out << "      <boxB>" << m.anims[i].boundSphere.max << "</boxB>" << endl;
    out << "      <rad>" << m.anims[i].boundSphere.radius << "</rad>" << endl;
    out << "      <NextAnimation>" << m.anims[i].NextAnimation << "</NextAnimation>" << endl;
    out << "      <Index>" << m.anims[i].Index << "</Index>" << endl;
    out << "    </Animation>" << endl;
  }
  out << "  </Animations>" << endl;

  out << "  <AnimationLookups size=\"" << m.animLookups.size() << "\">" << endl;
  for (size_t i = 0; i < m.animLookups.size(); i++)
    out << "    <AnimationLookup id=\"" << i << "\">" << m.animLookups[i] << "</AnimationLookup>" << endl;
  out << "  </AnimationLookups>" << endl;

  out << "  <Bones size=\"" << m.bones.size() << "\">" << endl;
  for (size_t i = 0; i < m.bones.size(); i++)
  {
    out << "    <Bone id=\"" << i << "\">" << endl;
    out << "      <keyboneid>" << m.bones[i].boneDef.keyboneid << "</keyboneid>" << endl;
    out << "      <billboard>" << m.bones[i].billboard << "</billboard>" << endl;
    out << "      <parent>" << m.bones[i].boneDef.parent << "</parent>" << endl;
    out << "      <geoid>" << m.bones[i].boneDef.geoid << "</geoid>" << endl;
    out << "      <unknown>" << m.bones[i].boneDef.unknown << "</unknown>" << endl;
#if 1 // too huge
    // AB translation
    out << "      <trans>" << endl;
    out << m.bones[i].trans;
    out << "      </trans>" << endl;
    // AB rotation
    out << "      <rot>" << endl;
    out << m.bones[i].rot;
    out << "      </rot>" << endl;
    // AB scaling
    out << "      <scale>" << endl;
    out << m.bones[i].scale;
    out << "      </scale>" << endl;
#endif
    out << "      <pivot>" << m.bones[i].boneDef.pivot << "</pivot>" << endl;
    out << "    </Bone>" << endl;
  }
  out << "  </Bones>" << endl;

  //	out << "  <BoneLookups size=\"" << m.header.nBoneLookup << "\">" << endl;
  //	uint16 *boneLookup = (uint16 *)(f.getBuffer() + m.header.ofsBoneLookup);
  //	for(size_t i=0; i<m.header.nBoneLookup; i++) {
  //		out << "    <BoneLookup id=\"" << i << "\">" << boneLookup[i] << "</BoneLookup>" << endl;
  //	}
  //	out << "  </BoneLookups>" << endl;

  out << "  <KeyBoneLookups size=\"" << m.header.nKeyBoneLookup << "\">" << endl;
  for (size_t i = 0; i < m.header.nKeyBoneLookup; i++)
    out << "    <KeyBoneLookup id=\"" << i << "\">" << m.keyBoneLookup[i] << "</KeyBoneLookup>" << endl;
  out << "  </KeyBoneLookups>" << endl;

  out << "  </SkeletonAndAnimation>" << endl;

  out << "  <GeometryAndRendering>" << endl;

  //	out << "  <Vertices size=\"" << m.header.nVertices << "\">" << endl;
  //	ModelVertex *verts = (ModelVertex*)(f.getBuffer() + m.header.ofsVertices);
  //	for(uint32 i=0; i<m.header.nVertices; i++) {
  //		out << "    <Vertice id=\"" << i << "\">" << endl;
  //		out << "      <pos>" << verts[i].pos << "</pos>" << endl; // TODO
  //		out << "    </Vertice>" << endl;
  //	}
  out << "  </Vertices>" << endl; // TODO
  out << "  <Views>" << endl;

  //	out << "  <Indices size=\"" << view->nIndex << "\">" << endl;
  //	out << "  </Indices>" << endl; // TODO
  //	out << "  <Triangles size=\""<< view->nTris << "\">" << endl;
  //	out << "  </Triangles>" << endl; // TODO
  //	out << "  <Properties size=\"" << view->nProps << "\">" << endl;
  //	out << "  </Properties>" << endl; // TODO
  //	out << "  <Subs size=\"" << view->nSub << "\">" << endl;
  //	out << "  </Subs>" << endl; // TODO

  out << "	<RenderPasses size=\"" << m.passes.size() << "\">" << endl;
  for (size_t i = 0; i < m.passes.size(); i++)
  {
    out << "	  <RenderPass id=\"" << i << "\">" << endl;
    ModelRenderPass * p = m.passes[i];
    ModelGeosetHD * geoset = m.geosets[p->geoIndex];
    out << "      <indexStart>" << geoset->istart << "</indexStart>" << endl;
    out << "      <indexCount>" << geoset->icount << "</indexCount>" << endl;
    out << "      <vertexStart>" << geoset->vstart << "</vertexStart>" << endl;
    out << "      <vertexEnd>" << geoset->vstart + geoset->vcount << "</vertexEnd>" << endl;
    out << "      <tex>" << p->tex << "</tex>" << endl;
    if (p->tex >= 0)
      out << "      <texName>" << TEXTUREMANAGER.get(p->tex).toStdString() << "</texName>" << endl;
    out << "      <useTex2>" << p->useTex2 << "</useTex2>" << endl;
    out << "      <useEnvMap>" << p->useEnvMap << "</useEnvMap>" << endl;
    out << "      <cull>" << p->cull << "</cull>" << endl;
    out << "      <trans>" << p->trans << "</trans>" << endl;
    out << "      <unlit>" << p->unlit << "</unlit>" << endl;
    out << "      <noZWrite>" << p->noZWrite << "</noZWrite>" << endl;
    out << "      <billboard>" << p->billboard << "</billboard>" << endl;
    out << "      <texanim>" << p->texanim << "</texanim>" << endl;
    out << "      <color>" << p->color << "</color>" << endl;
    out << "      <opacity>" << p->opacity << "</opacity>" << endl;
    out << "      <blendmode>" << p->blendmode << "</blendmode>" << endl;
    out << "      <geoset>" << geoset->id << "</geoset>" << endl;
    out << "      <swrap>" << p->swrap << "</swrap>" << endl;
    out << "      <twrap>" << p->twrap << "</twrap>" << endl;
    out << "      <ocol>" << p->ocol << "</ocol>" << endl;
    out << "      <ecol>" << p->ecol << "</ecol>" << endl;
    out << "	  </RenderPass>" << endl;
  }
  out << "	</RenderPasses>" << endl;

  out << "	<Geosets size=\"" << m.geosets.size() << "\">" << endl;
  for (size_t i = 0; i < m.geosets.size(); i++)
  {
    out << "	  <Geoset id=\"" << i << "\">" << endl;
    out << "      <id>" << m.geosets[i]->id << "</id>" << endl;
    out << "      <vstart>" << m.geosets[i]->vstart << "</vstart>" << endl;
    out << "      <vcount>" << m.geosets[i]->vcount << "</vcount>" << endl;
    out << "      <istart>" << m.geosets[i]->istart << "</istart>" << endl;
    out << "      <icount>" << m.geosets[i]->icount << "</icount>" << endl;
    out << "      <nSkinnedBones>" << m.geosets[i]->nSkinnedBones << "</nSkinnedBones>" << endl;
    out << "      <StartBones>" << m.geosets[i]->StartBones << "</StartBones>" << endl;
    out << "      <rootBone>" << m.geosets[i]->rootBone << "</rootBone>" << endl;
    out << "      <nBones>" << m.geosets[i]->nBones << "</nBones>" << endl;
    out << "      <BoundingBox>" << m.geosets[i]->BoundingBox[0] << "</BoundingBox>" << endl;
    out << "      <BoundingBox>" << m.geosets[i]->BoundingBox[1] << "</BoundingBox>" << endl;
    out << "      <radius>" << m.geosets[i]->radius << "</radius>" << endl;
    out << "	  </Geoset>" << endl;
  }
  out << "	</Geosets>" << endl;

  //	ModelTexUnit *tex = (ModelTexUnit*)(g.getBuffer() + view->ofsTex);
  //	out << "	<TexUnits size=\"" << view->nTex << "\">" << endl;
  //	for (size_t i=0; i<view->nTex; i++) {
  //		out << "	  <TexUnit id=\"" << i << "\">" << endl;
  //		out << "      <flags>" << tex[i].flags << "</flags>" << endl;
  //		out << "      <shading>" << tex[i].shading << "</shading>" << endl;
  //		out << "      <op>" << tex[i].op << "</op>" << endl;
  //		out << "      <op2>" << tex[i].op2 << "</op2>" << endl;
  //		out << "      <colorIndex>" << tex[i].colorIndex << "</colorIndex>" << endl;
  //		out << "      <flagsIndex>" << tex[i].flagsIndex << "</flagsIndex>" << endl;
  //		out << "      <texunit>" << tex[i].texunit << "</texunit>" << endl;
  //		out << "      <mode>" << tex[i].mode << "</mode>" << endl;
  //		out << "      <textureid>" << tex[i].textureid << "</textureid>" << endl;
  //		out << "      <texunit2>" << tex[i].texunit2 << "</texunit2>" << endl;
  //		out << "      <transid>" << tex[i].transid << "</transid>" << endl;
  //		out << "      <texanimid>" << tex[i].texanimid << "</texanimid>" << endl;
  //		out << "	  </TexUnit>" << endl;
  //	}
  //	out << "	</TexUnits>" << endl;

  out << "  </Views>" << endl;

  out << "  <RenderFlags></RenderFlags>" << endl;

  out << "	<Colors size=\"" << m.colors.size() << "\">" << endl;
  for (uint i = 0; i < m.colors.size(); i++)
  {
    out << "    <Color id=\"" << i << "\">" << endl;
    // AB color
    out << "    <color>" << endl;
    out << m.colors[i].color;
    out << "    </color>" << endl;
    // AB opacity
    out << "    <opacity>" << endl;
    out << m.colors[i].opacity;
    out << "    </opacity>" << endl;
    out << "    </Color>" << endl;
  }
  out << "	</Colors>" << endl;

  out << "	<Transparency size=\"" << m.transparency.size() << "\">" << endl;
  for (uint i = 0; i < m.transparency.size(); i++)
  {
    out << "    <Tran id=\"" << i << "\">" << endl;
    // AB trans
    out << "    <trans>" << endl;
    out << m.transparency[i].trans;
    out << "    </trans>" << endl;
    out << "    </Tran>" << endl;
  }
  out << "	</Transparency>" << endl;

  out << "  <TransparencyLookup></TransparencyLookup>" << endl;

  //	ModelTextureDef *texdef = (ModelTextureDef*)(f.getBuffer() + m.header.ofsTextures);
  //	out << "	<Textures size=\"" << m.header.nTextures << "\">" << endl;
  //	for(size_t i=0; i<m.header.nTextures; i++) {
  //		out << "	  <Texture id=\"" << i << "\">" << endl;
  //		out << "      <type>" << texdef[i].type << "</type>" << endl;
  //		out << "      <flags>" << texdef[i].flags << "</flags>" << endl;
  //		//out << "      <nameLen>" << texdef[i].nameLen << "</nameLen>" << endl;
  //		//out << "      <nameOfs>" << texdef[i].nameOfs << "</nameOfs>" << endl;
  //		if (texdef[i].type == TEXTURE_FILENAME)
  //			out << "		<name>" << f.getBuffer()+texdef[i].nameOfs  << "</name>" << endl;
  //		out << "	  </Texture>" << endl;
  //	}
  //	out << "	</Textures>" << endl;

  //	out << "  <TexLookups size=\"" << m.header.nTexLookup << "\">" << endl;
  //	uint16 *texLookup = (uint16 *)(f.getBuffer() + m.header.ofsTexLookup);
  //	for(size_t i=0; i<m.header.nTexLookup; i++) {
  //		out << "    <TexLookup id=\"" << i << "\">" << texLookup[i] << "</TexLookup>" << endl;
  //	}
  //	out << "  </TexLookups>" << endl;

  out << "	<ReplacableTextureLookup></ReplacableTextureLookup>" << endl;

  out << "  </GeometryAndRendering>" << endl;

  out << "  <Effects>" << endl;

  out << "	<TexAnims size=\"" << m.texAnims.size() << "\">" << endl;
  for (uint i = 0; i < m.texAnims.size(); i++)
  {
    out << "	  <TexAnim id=\"" << i << "\">" << endl;
    // AB trans
    out << "    <trans>" << endl;
    out << m.texAnims[i].trans;
    out << "    </trans>" << endl;
    // AB rot
    out << "    <rot>" << endl;
    out << m.texAnims[i].rot;
    out << "    </rot>" << endl;
    // AB scale
    out << "    <scale>" << endl;
    out << m.texAnims[i].scale;
    out << "    </scale>" << endl;
    out << "	  </TexAnim>" << endl;
  }
  out << "	</TexAnims>" << endl;

  out << "	<RibbonEmitters></RibbonEmitters>" << endl; // TODO

  out << "	<Particles size=\"" << m.header.nParticleEmitters << "\">" << endl;
  for (size_t i = 0; i < m.particleSystems.size(); i++)
  {
    out << "	  <Particle id=\"" << i << "\">" << endl;
    out << m.particleSystems[i];
    out << "	  </Particle>" << endl;
  }
  out << "	</Particles>" << endl;

  out << "  </Effects>" << endl;

  out << "	<Miscellaneous>" << endl;

  out << "	<BoundingVolumes></BoundingVolumes>" << endl;
  out << "	<Lights></Lights>" << endl;
  out << "	<Cameras></Cameras>" << endl;

  out << "	<Attachments size=\"" << m.header.nAttachments << "\">" << endl;
  for (size_t i = 0; i < m.header.nAttachments; i++)
  {
    out << "	  <Attachment id=\"" << i << "\">" << endl;
    out << "      <id>" << m.atts[i].id << "</id>" << endl;
    out << "      <bone>" << m.atts[i].bone << "</bone>" << endl;
    out << "      <pos>" << m.atts[i].pos << "</pos>" << endl;
    out << "	  </Attachment>" << endl;
  }
  out << "	</Attachments>" << endl;

  out << "  <AttachLookups size=\"" << m.header.nAttachLookup << "\">" << endl;
  //	int16 *attachLookup = (int16 *)(f.getBuffer() + m.header.ofsAttachLookup);
  //	for(size_t i=0; i<m.header.nAttachLookup; i++) {
  //		out << "    <AttachLookup id=\"" << i << "\">" << attachLookup[i] << "</AttachLookup>" << endl;
  //	}
  //	out << "  </AttachLookups>" << endl;

  out << "	<Events size=\"" << m.events.size() << "\">" << endl;
  for (size_t i = 0; i < m.events.size(); i++)
  {
    out << "	  <Event id=\"" << i << "\">" << endl;
    out << m.events[i];
    out << "	  </Event>" << endl;
  }
  out << "	</Events>" << endl;

  out << "	</Miscellaneous>" << endl;

  //	out << "    <>" << m.header. << "</>" << endl;
  out << "  <TextureLists>" << endl;
  for (auto it : m.passes)
  {
    GLuint tex = m.getGLTexture(it->tex);
    if (tex != ModelRenderPass::INVALID_TEX)
      out << "    <TextureList id=\"" << tex << "\">" << TEXTUREMANAGER.get(tex).toStdString() << "</TextureList>" << endl;
  }
  out << "  </TextureLists>" << endl;

  out << "</m2>" << endl;

  return out;
}
