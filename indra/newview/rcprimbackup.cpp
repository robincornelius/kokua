 /*
 * Copyright (C) 2010, Robin Cornelius <robin.cornelius@gmail.com>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

 /* LLSD to HPA exporter
 * Copyright (C) 2009,2010 Patrick Sapinski 
   Licenced under the GNU GPL v2
 */

 /* -- Description --

 A Prim XML/LLSD/HPA exporter that obeys Linden Labs Third party viewer export controls by checking
 UUID of each exported prim and asset against the current user and only exporting what you have created.
 This is a rewrite on the V2 code base from scratch apart from the HPA export function as detailed above.

 The exporter queues requests and retries after a time out and abandons after a set amount of retries.
 
 This exporter can also export textures in SL, it does this by looking in your inventory for the same texture
 UUID and if matched, checks the asset creator there, if that is you it exports the texture as well. Normally
 textures do not have creator info so the inventory check is a work around to ensure you created the texture.
 It currently only searches the "Textures" folder and no sub folders.

 The exporter always requests properties from the export set, this is in order to be 100% sure we have the permissions
 info which contains the creator used for export permission checking on SL

 When properties are recieved if the permission check passes inventory for the prim is requested. When inventory is 
 recieved each asset is permission checked and if OK requested for download and saved.
 
/* 
	TODO: get Patrick to let us LGPL his function
	TODO: textures that vanish in world after export has started will freeze and not export
	TODO: let inventory search look in sub folders and other parts of inventory
	TODO: add grid manager support to turn off restrictions when on non LL grids
	TODO: stuck assets, some assets refuse to download at all, even when you are creator, and everything is in order.
			seems to be packet loss related issues.
	TODO: remember export inventory and export texture settings in exportFloater
*/

#include "llviewerprecompiledheaders.h"

#include "llfilepicker.h"
#include "llcallbacklist.h" //gIdleCallbacks

#include "llagent.h" // gAgent
#include "llviewerobjectlist.h" //gObjectList;

#include "llinventory.h"
#include "roles_constants.h" // for GP_OBJECT_MANIPULATE
#include "llviewerregion.h" 
#include "llvfs.h"
#include "llviewertexteditor.h"
#include "llviewertexturelist.h"

#include "lltextureentry.h"
#include "llimagej2c.h"
#include "llsdutil_math.h"
#include "llvotree.h"
#include "llvograss.h"
#include "llsdserialize.h"

// needed for hpa info
#include "llversionserver.h"
#include "llappviewer.h"
#include "lllogchat.h"

#include "llinventoryfunctions.h"

#include "rcprimbackup.h"


rcprimbackup::rcprimbackup()
{
	mExportTextures = true;
	mExportInventories = true;
	mSlGrid = true; //FIX WE with a grid manager

	if(mSlGrid)
	{
		// Some one wanted us, probably the exportFloater, request the textures inventory now
		// whist the user fafs about then we will have it in time for perm checks
		LLUUID texturesFolder = gInventory.findCategoryUUIDForType(LLFolderType::FT_TEXTURE,false,false);
		gInventory.fetchDescendentsOf(texturesFolder);
	}
}

rcprimbackup::~rcprimbackup()
{

}

void rcprimbackup::resetAll()
{
	mObjectsToExport.clear();
	mRootObjectsToExport.clear();
	mPropertiesWaitQueue.clear();
	mInventoryWaitQueue.clear();
	recieved_properties_map.clear();
	mAssetWaitQueue.clear();
	mTexturesQueue.clear();
	mLLSDPrimData.clear();
	recieved_inventory_map.clear();

	mTotalTextures = 0;
	mTexturesRecieved = 0;
	mTotalAssets = 0;
	mAssetsRecieved = 0;

	mXmlExported = 0;
	mActiveAssetRequests = 0;

	mTotalToExport = 0;

	gIdleCallbacks.deleteFunction(rcprimbackup::exportworker,this);

	mIsFinished = false;
	mSaved = false;
}

void rcprimbackup::exportSelectedObjects()
{

	resetAll();

	// Open the file save dialog
	LLFilePicker& file_picker = LLFilePicker::instance();
	if( !file_picker.getSaveFile( LLFilePicker::FFSAVE_XML ) )
	{
		// User canceled save.
		return;
	}

	mFileName = file_picker.getCurFile();
	mFolderName = gDirUtilp->getDirName(mFileName);

	if(!LLFile::isdir(mFolderName+gDirUtilp->getDirDelimiter()+"inventory"))
	{
		LLFile::mkdir(mFolderName+gDirUtilp->getDirDelimiter()+"inventory");
	}

	if(!LLFile::isdir(mFolderName+gDirUtilp->getDirDelimiter()+"textures"))
	{
		LLFile::mkdir(mFolderName+gDirUtilp->getDirDelimiter()+"textures");
	}

	//exportFloater::show(LLUUID::null);

	LLSelectMgr::getInstance()->getSelection()->ref();

	mBoundingBox = LLSelectMgr::getInstance()->getBBoxOfSelection();
	mCenter = mBoundingBox.getCenterAgent();

	struct ff : public LLSelectedObjectFunctor
	{
		virtual bool apply(LLViewerObject* object)
		{
			rcprimbackup::getInstance()->mRootObjectsToExport.insert(object);
			return true;
		}
	} func;

		struct ffF : public LLSelectedObjectFunctor
	{
		virtual bool apply(LLViewerObject* object)
		{
			object->mGotInventory = false;
			object->mGotProperties = false;
			rcprimbackup::getInstance()->mObjectsToExport.insert(object);
			return true;
		}
	} func2;

	LLSelectMgr::getInstance()->getSelection()->applyToRootObjects(&func);
	LLSelectMgr::getInstance()->getSelection()->applyToObjects(&func2);

	mTotalToExport = LLSelectMgr::getInstance()->getSelection()->getObjectCount();
	
	LLSelectMgr::getInstance()->getSelection()->unref();

	if(mExportTextures)
	{
		populateTexturesQueue();
	}

	gIdleCallbacks.addFunction(rcprimbackup::exportworker,this);
}

// static
bool rcprimbackup::check_perms( LLSelectNode* node )
{
	LLPermissions *perms = node->mPermissions;
	return (gAgent.getID() == perms->getOwner() &&
	        gAgent.getID() == perms->getCreator() &&
	        (PERM_ITEM_UNRESTRICTED &
	         perms->getMaskOwner()) == PERM_ITEM_UNRESTRICTED);
}

//static
void rcprimbackup::exportworker(void *userdata)
{
	rcprimbackup * pthis = (rcprimbackup*) userdata;

	std::set <LLPointer <LLViewerObject> >::iterator iter;

	for(iter = pthis->mObjectsToExport.begin(); iter != pthis->mObjectsToExport.end();iter++)
	{

		if((*iter)->mGotProperties)
			continue;

		// this is duff, pretend we got it
		if((*iter)->mPropRequestCount>10)
		{
			(*iter)->mGotProperties=true;
			continue;
		}

		 if(pthis->mPropRequestQueue>100)
			continue;

		if((*iter)->mPropRequestTime < (time(NULL)-(60)))
		{
			 pthis->mPropertiesWaitQueue.push_back(*iter);
			 pthis->requestPrimProperties((*iter)->mLocalID);
			 (*iter)->mPropRequestTime = time(NULL);
			 (*iter)->mPropRequestCount++;
			 pthis->mPropRequestQueue++;
		}

	}

	for(iter = pthis->mObjectsToExport.begin(); iter != pthis->mObjectsToExport.end();iter++)
	{

		LLPCode pcode = (*iter)->getPCode();
		if (pcode != LL_PCODE_VOLUME)
			continue;

		if((*iter)->mGotInventory)
			continue;

		// this is duff, pretend we got it
		if((*iter)->mInvRequestCount>10)
		{
			(*iter)->mGotInventory=true;
			rcprimbackup::getInstance()->mExportInventories++;
			continue;
		}

		 if(pthis->mInvRequestQueue>100)
			 continue;

		if((*iter)->mInvRequestTime < (time(NULL)-(60)))
		{
			 pthis->mInventoryWaitQueue.push_back(*iter);
			 pthis->requestPrimInventory((*iter));
			 (*iter)->mInvRequestTime = time(NULL);
			 (*iter)->mInvRequestCount++;
			 pthis->mInvRequestQueue++;
		}
	}

	std::vector <assetRequest_t *>::iterator assetiter = rcprimbackup::getInstance()->mAssetWaitQueue.begin();
	for( assetiter; assetiter!=rcprimbackup::getInstance()->mAssetWaitQueue.end(); assetiter++)
	{
		if( (*assetiter)->mNoAttempts>5 )
		{
			// give up on this asset
			assetRequest_t * req = (*assetiter);
			llwarns << "Giving up on " << req->id << "after "<<req->mNoAttempts<<" attempts"<<llendl;
			rcprimbackup::getInstance()->mAssetWaitQueue.erase(assetiter);
			delete(req);
			rcprimbackup::getInstance()->mActiveAssetRequests--;
			rcprimbackup::getInstance()->mAssetsRecieved++;
			break;
		}

		 if(pthis->mActiveAssetRequests>10)
			continue;

		 time_t now=time(NULL);
		 time_t last=(*assetiter)->mLastAttempt;

		 if( last+60 <now)
		 {
				rcprimbackup::getInstance()->downloadAsset(*assetiter);
		 }

	}

	for(iter = pthis->mRootObjectsToExport.begin(); iter != pthis->mRootObjectsToExport.end();iter++)
	{
		if((*iter)->mGotInventory && (*iter)->mGotProperties)
		{
	
			LLViewerObject::child_list_t child_list;
			child_list=(*iter)->getChildren();
			LLViewerObject::const_child_list_t::const_iterator childiter;
			bool allfound=true;
			for(childiter=child_list.begin(); childiter!=child_list.end(); childiter++)
			{
				if(!(*childiter)->mGotProperties || !(*childiter)->mGotInventory)
				{
					allfound = false;
					if(pthis->mObjectsToExport.count((*childiter))==0)
					{	
						//This child object came late to the party
						pthis->mObjectsToExport.insert((*childiter));
						// the worker will take care of this next itteration
					}
				}
			}

			if(allfound==true)
			{
				pthis->LinkSetToLLSD((*iter));
				// LLSD one link set then break LinkSetToLLSD will remove from the set;
				break;
			}
		}
	}

	if(!pthis->mSaved && pthis->mObjectsToExport.empty())
	{	
		pthis->saveLLSD();
		pthis->saveHPA();
		pthis->mSaved = true;
	}

	if(pthis->mSaved && rcprimbackup::getInstance()->mAssetWaitQueue.empty() && rcprimbackup::getInstance()->mTexturesQueue.empty())
	{
		pthis->mIsFinished = true;
		gIdleCallbacks.deleteFunction(rcprimbackup::exportworker,pthis);
	}

}

void rcprimbackup::requestPrimProperties(U32 localID)
{

	llinfos<<"Requesting prim properties for "<<localID<<llendl;

	gMessageSystem->newMessageFast(_PREHASH_ObjectSelect);
	gMessageSystem->nextBlockFast(_PREHASH_AgentData);
	gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
	gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
	gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID, localID);

    gMessageSystem->sendReliable(gAgent.getRegionHost());
}

void rcprimbackup::gotPrimProperties(LLSelectNode * node,LLUUID id, bool needDelete)
{

	std::vector <LLPointer <LLViewerObject> >::iterator iter;
	bool found = false;
	for(iter = mPropertiesWaitQueue.begin() ; iter !=mPropertiesWaitQueue.end() ; iter++)
	{
		if((*iter)->getID()==id)
		{
			mPropertiesWaitQueue.erase(iter);
			found = true;
			break;
		}
	}

	if( found == false)
	{
		return;
	}

	LLViewerObject* object = gObjectList.findObject(id);
	object->mGotProperties = true;
	mPropRequestQueue--;

	llinfos<<"got prim properties for "<<object->getLocalID()<<llendl;

	LLSD * props=new LLSD();

	(*props)["creator_id"] = node->mPermissions->getCreator().asString();
	(*props)["owner_id"] = node->mPermissions->getOwner().asString();
	(*props)["group_id"] = node->mPermissions->getGroup().asString();
	(*props)["last_owner_id"] = node->mPermissions->getLastOwner().asString();

	(*props)["base_mask"] = llformat("%d",node->mPermissions->getMaskBase());
	(*props)["owner_mask"] = llformat("%d",node->mPermissions->getMaskOwner());
	(*props)["group_mask"] = llformat("%d",node->mPermissions->getMaskGroup());
	(*props)["everyone_mask"] = llformat("%d",node->mPermissions->getMaskEveryone());
	(*props)["next_owner_mask"] = llformat("%d",node->mPermissions->getMaskNextOwner());
	(*props)["sale_info"] = node->mSaleInfo.asLLSD();

	(*props)["creation_date"] = llformat("%d",node->mCreationDate);
	(*props)["inv_serial"] = (S32)node->mInventorySerial;
	(*props)["item_id"] = node->mItemID.asString();
	(*props)["folder_id"] = node->mFolderID.asString();
	(*props)["from_task_id"] = node->mFromTaskID.asString();
	(*props)["name"] = node->mName;
	(*props)["description"] = node->mDescription;
	(*props)["touch_name"] = node->mTouchName;
	(*props)["sit_name"] = node->mSitName;
	

	if(recieved_properties_map.find(id)==recieved_properties_map.end())
	{
		recieved_properties_map[id]=props;
	}
	else
	{
		llwarns << "properties recieved for entry already in map" <<llendl;
	}

	if(needDelete)
		delete node;
}

void rcprimbackup::requestPrimInventory(LLViewerObject * obj)
{
	llinfos<<"Requesting prim inventory for "<<obj->getLocalID()<<llendl;

	if(!mExportInventories)
	{
		obj->mGotInventory = true;
		return;
	}

	LLPCode pcode = obj->getPCode();
	if (pcode == LL_PCODE_VOLUME)
	{
		obj->removeInventoryListener(this);
		obj->registerInventoryListener(this,NULL);

		obj->dirtyInventory();
		obj->requestInventory();
	}
}


void rcprimbackup::inventoryChanged(LLViewerObject* obj,
										LLInventoryObject::object_list_t* inv,
										S32,
										void* q_id)
{

	llinfos<<"got prim inventory for "<<obj->getLocalID()<<llendl;
	obj->mGotInventory = true;

	std::vector <LLPointer <LLViewerObject> >::iterator iter;
	iter = mInventoryWaitQueue.begin();

	bool found = false;

	for(iter;iter!=mInventoryWaitQueue.end();iter++)
	{
		if( (*iter)->getID() == obj->getID() )
		{
			mInventoryWaitQueue.erase(iter);
			found = true;
			break;
		}
	}

	if( found == false )
	{
		llinfos<<"got unexpected prim inventory for "<<obj->getLocalID()<<llendl;	
		return;
	}

	// Do something with the inventory we recieved

	LLInventoryObject::object_list_t::iterator it;
	it = inv->begin();

	LLSD * inventory = new LLSD();
	int count=0;

	for(it;it!=inv->end();it++)
	{
		LLInventoryObject* asset = (*it);

		if(!asset || asset->getType()==LLAssetType::AT_CATEGORY)
			continue;

		LLInventoryItem* item = (LLInventoryItem*)((LLInventoryObject*)(*it));

		LLPermissions perm = item->getPermissions();
		
		if(canexportasset(perm,asset->getType()))
		{
			LLSD inv_item;
			inv_item["name"] = asset->getName();
			inv_item["type"] = LLAssetType::lookup(asset->getType());
			inv_item["desc"] = ((LLInventoryItem*)((LLInventoryObject*)(*it)))->getDescription();
			inv_item["item_id"] = item->getUUID().asString();
			(*inventory)[count++] = inv_item;
			
			queueForDownload((*it),obj,asset->getName());
			mTotalAssets++;

		}
	}

	recieved_inventory_map[obj->getID()] = inventory;
}

bool rcprimbackup::canexportasset(LLPermissions perm,LLAssetType::EType type)
{

	switch(type)
	{
		case LLAssetType::AT_OBJECT:
		case LLAssetType::AT_CALLINGCARD:
		case LLAssetType::AT_LINK:
		case LLAssetType::AT_LINK_FOLDER:
			return false;
			break;
		default:
		break;
	}

	if(mSlGrid)
	{
		if(perm.getCreator()!=gAgent.getID())
		{
			return false;
		}
	}

	// Even if we are the creator we may not be able to export if we are not the owner
	// check this out

	BOOL object_is_group_owned = FALSE;
	LLUUID object_owner_id;
	perm.getOwnership(object_owner_id, object_is_group_owned);

	// Calculate proxy_agent_id and group_id to use for permissions checks.
	// proxy_agent_id may be set to the object owner through group powers.
	// group_id can only be set to the object's group, if the agent is in that group.
	LLUUID group_id = LLUUID::null;
	LLUUID proxy_agent_id = gAgent.getID();

	// Gods can always operate.
	if (gAgent.isGodlike())
	{
		return TRUE;
	}

	// Check if the agent is in the same group as the object.
	LLUUID object_group_id = perm.getGroup();
	if (object_group_id.notNull() &&
		gAgent.isInGroup(object_group_id))
	{
		// Assume the object's group during this operation.
		group_id = object_group_id;
	}


	// Check if the agent can assume ownership through group proxy or agent-granted proxy.
	if (   ( object_is_group_owned 
			&& gAgent.hasPowerInGroup(object_owner_id, GP_OBJECT_MANIPULATE))
			// Only allow proxy for move, modify, and copy.
			|| ((!object_is_group_owned && gAgent.isGrantedProxy(perm))))
	{
		// This agent is able to assume the ownership role for this operation.
		proxy_agent_id = object_owner_id;
	}

	if(perm.allowOperationBy(PERM_MODIFY, proxy_agent_id, group_id) || 
		perm.allowOperationBy(PERM_COPY, proxy_agent_id, group_id))
	{

		if(proxy_agent_id != gAgent.getID())
		{
			// We are proxying the request we can only now access inventory if the Everyone or Group Masks are set
			if((perm.getMaskEveryone() & 0x00008000) == 0x00008000)
			{
				return true;
			}

			// We are proxying the request we can only now access inventory if the Everyone or Group Masks are set
			if((perm.getMaskGroup() & 0x00008000) == 0x00008000)
			{
				return true;
			}

			return false;
		}

		return true;
	}

	return false;
}


bool rcprimbackup::queueForDownload(LLInventoryObject* item, LLViewerObject* container, std::string iname)
{

	if(!item)
		return false;

	LLInventoryItem * iitem = dynamic_cast<LLInventoryItem*>(item);

	if(!iitem)
		return false;

	assetRequest_t * details = new assetRequest_t;

	llwarns << "** queueForDownload User data "<<details<<llendl;

	details->name = item->getName();
	details->id = iitem->getUUID();
	details->mNoAttempts = 0;
	details->mLastAttempt = 0;
	details->mAssetID = iitem->getAssetUUID();
	details->type= item->getType();
	details->perm = LLPermissions(((LLInventoryItem*)item)->getPermissions());
	details->container = container;

	mAssetWaitQueue.push_back(details);

	return true;
}

bool rcprimbackup::downloadAsset(assetRequest_t * request)
{
	llinfos << "Trying to download asset " << request->name <<" " << request->id << llendl;

	llwarns << "** downloadAsset User data "<<request<<llendl;
	//LLPermissions perm(((LLInventoryItem*)item)->getPermissions());

	request->mNoAttempts++;
	request->mLastAttempt=time(NULL);

	rcprimbackup::getInstance()->mActiveAssetRequests++;

	gAssetStorage->getInvItemAsset(request->container!=NULL ? request->container->getRegion()->getHost() : LLHost::invalid,
	gAgent.getID(),
	gAgent.getSessionID(),
	request->perm.getOwner(),
	request->container != NULL ? request->container->getID() : LLUUID::null,
	request->id,
	request->mAssetID,
	request->type,
	AssetRecievedCallback,
	request,
	TRUE);

	return true;

}

void rcprimbackup::AssetRecievedCallback(LLVFS *vfs, const LLUUID& uuid, LLAssetType::EType type, void *userdata, S32 result, LLExtStat extstat)
{
	assetRequest_t* details = (assetRequest_t*)userdata;
	llwarns << "** AssetRecievedCallback User data "<<userdata<<llendl;

	bool found = false;
	std::vector <assetRequest_t *>::iterator iter;
	for(iter=rcprimbackup::getInstance()->mAssetWaitQueue.begin();iter!=rcprimbackup::getInstance()->mAssetWaitQueue.end();iter++)
	{
		if((*iter)==details)
		{
			found = true;
			break;
		}
	}

	if(!found)
	{
		llwarns << "Recieved an asset callback for an asset we did not request, may be an old request?"<<llendl;
		return;
	}

	if(result == LL_ERR_NOERR)
	{

		S32 size = vfs->getSize(uuid, type);
		U8* buffer = new U8[size];
		vfs->getData(uuid, type, buffer, 0, size);

		if(type == LLAssetType::AT_NOTECARD)
		{
			LLViewerTextEditor::Params param;
			LLViewerTextEditor* edit = new LLViewerTextEditor(param);
		
			if(edit->importBuffer((char*)buffer, (S32)size))
			{
				std::string card = edit->getText();
				edit->die();
				delete buffer;
				buffer = new U8[card.size()];
				size = card.size();
				strcpy((char*)buffer,card.c_str());
			}
		}
		
		LLAPRFile assetfile;
		assetfile.open(rcprimbackup::getInstance()->mFolderName+gDirUtilp->getDirDelimiter()+"inventory"+gDirUtilp->getDirDelimiter()+details->id.asString(), LL_APR_WB);
		apr_file_t *fp = assetfile.getFileHandle();
		if(fp)
		{
			assetfile.write(buffer, size);
			assetfile.close();
		}

		llinfos << "Sucesfully Downloaded asset "<< details->name <<" "<<uuid<<llendl;
		details->mNoAttempts=1000;
	
	} 
	else 
	{
		if(result != LL_ERR_ASSET_REQUEST_FAILED)
		{
			llinfos << "Failed 1 to Downloaded asset "<< details->name <<" "<<uuid<<llendl;
			//cmdline_printchat("Failed to save file "+info->path+" ("+info->name+") : "+std::string(LLAssetStorage::getErrorString(result))+" giving up");
			return;
		}

		llinfos << "Failed 2 to Downloaded asset "<< details->name <<" "<<uuid<<llendl;
		//cmdline_printchat("Failed to save file "+info->path+" ("+info->name+") : "+std::string(LLAssetStorage::getErrorString(result))+" we should retry");

	}
}


void rcprimbackup::populateTexturesQueue()
{
	std::set <LLPointer <LLViewerObject> >::iterator iter;

	if(mSlGrid)
	{
		LLUUID texturesFolder = gInventory.findCategoryUUIDForType(LLFolderType::FT_TEXTURE,false,false);
		LLInventoryModel::cat_array_t* cats = 0;
		gInventory.getDirectDescendentsOf(texturesFolder,cats,mTextureInvItems);
	}

	for(iter = mObjectsToExport.begin(); iter != mObjectsToExport.end();iter++)
	{
		LLUUID id;
		U8 count = (*iter)->getNumTEs();
		for (U8 i = 0; i < count; i++)
		{
			id=(*iter)->getTEImage(i)->getID();
			addToTexturesQueue(id);	
		}

		if ((*iter)->getParameterEntryInUse(LLNetworkData::PARAMS_SCULPT))
		{
			LLSculptParams* sculpt = (LLSculptParams*)(*iter)->getParameterEntry(LLNetworkData::PARAMS_SCULPT);
			id=sculpt->getSculptTexture();
			addToTexturesQueue(id);
		}
	}
}

void rcprimbackup::addToTexturesQueue(LLUUID id)
{
	if(mTexturesQueue.count(id)==0)
	{
		bool permok = false;

		if(mSlGrid)
		{
			for (LLInventoryModel::item_array_t::const_iterator iter = mTextureInvItems->begin(); iter != mTextureInvItems->end(); ++iter)
			{
				if((*iter)->getAssetUUID()==id)
				{
					if((*iter)->getCreatorUUID()==gAgent.getID())
					{
						permok = true;
					}
				}
			}
		}
		else
		{
			permok = true;
		}	

		if(permok==false)
			return;

		mTotalTextures++;
		mTexturesQueue.insert(id);
		llinfos << "Adding image "<< id<<" to queue"<<llendl;

		LLViewerFetchedTexture * fimg;
		fimg = LLViewerTextureManager::getFetchedTexture(id, MIPMAP_TRUE, LLViewerTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
		fimg->setBoostLevel(LLViewerTexture::BOOST_PREVIEW);
		fimg->forceToSaveRawImage(0);
		fimg->addTextureStats( (F32)MAX_IMAGE_AREA );
		
		fimg->setLoadedCallback(&rcprimbackup::onFileLoadedForSave, 0, TRUE, FALSE, NULL, &mCallbackTextureList);
	}
}

void rcprimbackup::onFileLoadedForSave(BOOL success, 
					  LLViewerFetchedTexture *src_vi,
					  LLImageRaw* src, 
					  LLImageRaw* aux_src, 
					  S32 discard_level,
					  BOOL final,
					  void* userdata)
{

	llinfos << "Loaded callback for image "<< src_vi->getID()<< "at discard " << src_vi->getDiscardLevel()<<llendl;

	if(!final || !success)
		return;

	if(!src)
	{
		src = src_vi->getRawImage();
		if(src==NULL)
		{
			llerrs << "WTF?"  << llendl;
		}
	}

	LLPointer<LLImageJ2C> image_j2c = new LLImageJ2C();
	//image_j2c->setReversible(src_vi->mExportReversable);
	if(!image_j2c->encode(src,0.0))
	{
		llwarns << "Failed to encode image" << src_vi->getID() << llendl;
	}
	else if(!image_j2c->save( rcprimbackup::getInstance()->mFolderName +  gDirUtilp->getDirDelimiter() + "textures"+ gDirUtilp->getDirDelimiter()+src_vi->getID().asString()+".j2c" ))
	{
		llwarns << "Failed to save image " << src_vi->getID() << " to disk" << llendl;
	}
	else
	{
		llinfos << "** Saved image " << src_vi->getID() << " to disk" << llendl;
	}

	if(rcprimbackup::getInstance()->mTexturesQueue.count(src_vi->getID())>0)
	{
		rcprimbackup::getInstance()->mTexturesQueue.erase(src_vi->getID());
	}

	rcprimbackup::getInstance()->mTexturesRecieved++;
}

void rcprimbackup::LinkSetToLLSD(LLPointer<LLViewerObject> obj)
{

	LLViewerObject::child_list_t child_list;
	child_list=obj->getChildren();
	LLViewerObject::const_child_list_t::const_iterator childiter;
	
	LLSD * linkset = new LLSD();
	LLSD parent;
	if(PrimToLLSD(obj,true,parent))
	{
		(*linkset)[0]=parent; 
		int count;
		for(count=1,childiter=child_list.begin(); childiter!=child_list.end(); childiter++,count++)
		{
			LLSD child;
			if(PrimToLLSD((*childiter),false,child))
			{
				(*linkset)[count]=child;
			}

			mObjectsToExport.erase((*childiter));
		}

		mLLSDPrimData.push_back(linkset);
	}
	else
	{
		for(childiter=child_list.begin(); childiter!=child_list.end(); childiter++)
		{
			mObjectsToExport.erase((*childiter));
		}
	}

	mObjectsToExport.erase(obj);
	mRootObjectsToExport.erase(obj);
}

bool rcprimbackup::PrimToLLSD(LLPointer<LLViewerObject> obj,bool parent,LLSD &prim_llsd)
{

	if(recieved_properties_map.count(obj->getID())==1)
	{
		LLSD * properties = recieved_properties_map[obj->getID()];
		
		if(mSlGrid)
		{
			if((*properties)["creator_id"].asString() != gAgent.getID().asString())
			{
				mXmlExported++;
				return false;
			}
		}

		prim_llsd["properties"] = *properties;
	}

	if(parent)
	{
		prim_llsd["position"] = obj->getPositionRegion().getValue();
		prim_llsd["rotation"] = ll_sd_from_quaternion(obj->getRotation());
	}
	else
	{
		prim_llsd["position"] = obj->getPositionRegion().getValue();
		prim_llsd["rotation"] = ll_sd_from_quaternion(obj->getRotationEdit());
	}

	// Transforms
	prim_llsd["scale"] = obj->getScale().getValue();

	// Flags
	prim_llsd["shadows"] = obj->flagCastShadows();
	prim_llsd["phantom"] = obj->flagPhantom();
	prim_llsd["physical"] = (BOOL)(obj->mFlags & FLAGS_USE_PHYSICS);

	// For saving tree and grass positions store the pcode so we 
	// know what to restore and the state is the species
	LLPCode pcode = obj->getPCode();
	prim_llsd["pcode"] = pcode;
	
	if(pcode==LL_PCODE_LEGACY_TREE)
	{
		LLVOTree* tree=dynamic_cast<LLVOTree*>(obj.get());
		if(tree)
		{
			prim_llsd["species"] = tree->getSpecies();
		}
	}

	if(pcode==LL_PCODE_LEGACY_GRASS)
	{
		LLVOGrass* grass=dynamic_cast<LLVOGrass*>(obj.get());
		if(grass)
		{
			prim_llsd["species"] = grass->getSpecies();
		}
	}

	// Only volumes have all the prim parameters
	if(LL_PCODE_VOLUME == pcode) 
	{
		LLVolumeParams params = obj->getVolume()->getParams();
		prim_llsd["volume"] = params.asLLSD();

		if (obj->isFlexible())
		{
			LLFlexibleObjectData* flex = (LLFlexibleObjectData*)obj->getParameterEntry(LLNetworkData::PARAMS_FLEXIBLE);
			prim_llsd["flexible"] = flex->asLLSD();
		}
		if (obj->getParameterEntryInUse(LLNetworkData::PARAMS_LIGHT))
		{
			LLLightParams* light = (LLLightParams*)obj->getParameterEntry(LLNetworkData::PARAMS_LIGHT);
			prim_llsd["light"] = light->asLLSD();
		}
		if (obj->getParameterEntryInUse(LLNetworkData::PARAMS_SCULPT))
		{
			LLSculptParams* sculpt = (LLSculptParams*)obj->getParameterEntry(LLNetworkData::PARAMS_SCULPT);
			prim_llsd["sculpt"] = sculpt->asLLSD();
		}
	}

	// Textures
	LLSD te_llsd;
	U8 te_count = obj->getNumTEs();
		
	for (U8 i = 0; i < te_count; i++)
	{
		te_llsd.append(obj->getTE(i)->asLLSD());
	}
	
	prim_llsd["textures"] = te_llsd;

	prim_llsd["id"] = obj->getID().asString();

	if(recieved_inventory_map.count(obj->getID())==1)
	{
		LLSD * inventory = recieved_inventory_map[obj->getID()];
		prim_llsd["inventory"] = *inventory;
	}

	mXmlExported++;

	return true;
}

void rcprimbackup::saveLLSD()
{

	LLSD file;
	LLSD header;
	header["Version"] = 2;
	file["Header"] = header;
//	file["Grid"] = grid_uri;

	std::list<LLSD *>::iterator iter;
	int index;
	for(index=0,iter=mLLSDPrimData.begin();iter!=mLLSDPrimData.end();iter++,index++)
	{
		file["Objects"][index] = (*(*iter));
	}

	llofstream export_file(mFileName + ".llsd",std::ios_base::app | std::ios_base::out);
	LLSDSerialize::toPrettyXML(file, export_file);
	export_file.close();

}

void rcprimbackup::saveHPA()
{

	LLXMLNode *project_xml = new LLXMLNode("project", FALSE);
														
	project_xml->createChild("schema", FALSE)->setValue("1.0");
	project_xml->createChild("name", FALSE)->setValue(gDirUtilp->getBaseFileName(mFileName, false));
	project_xml->createChild("date", FALSE)->setValue(LLLogChat::timestamp(1));
	project_xml->createChild("software", FALSE)->setValue(llformat("%s %d.%d.%d.%d", LLAppViewer::instance()->getSecondLifeTitle().c_str(), LL_VERSION_MAJOR, LL_VERSION_MINOR, LL_VERSION_PATCH, LL_VERSION_BUILD));
	project_xml->createChild("platform", FALSE)->setValue("Second Life");
	
	//Fix me when we have a login manager support grids here
	project_xml->createChild("grid", FALSE)->setValue("Agni");

	LLXMLNode *group_xml;
	group_xml = new LLXMLNode("group",FALSE);

	LLVector3 max = mCenter + mBoundingBox.getExtentLocal() / 2;
	LLVector3 min = mCenter - mBoundingBox.getExtentLocal() / 2;

	LLXMLNodePtr max_xml = group_xml->createChild("max", FALSE);
	max_xml->createChild("x", TRUE)->setValue(llformat("%.5f", max.mV[VX]));
	max_xml->createChild("y", TRUE)->setValue(llformat("%.5f", max.mV[VY]));
	max_xml->createChild("z", TRUE)->setValue(llformat("%.5f", max.mV[VZ]));

	LLXMLNodePtr min_xml = group_xml->createChild("min", FALSE);
	min_xml->createChild("x", TRUE)->setValue(llformat("%.5f", min.mV[VX]));
	min_xml->createChild("y", TRUE)->setValue(llformat("%.5f", min.mV[VY]));
	min_xml->createChild("z", TRUE)->setValue(llformat("%.5f", min.mV[VZ]));
											
	LLXMLNodePtr center_xml = group_xml->createChild("center", FALSE);
	center_xml->createChild("x", TRUE)->setValue(llformat("%.5f", mCenter.mV[VX]));
	center_xml->createChild("y", TRUE)->setValue(llformat("%.5f", mCenter.mV[VY]));
	center_xml->createChild("z", TRUE)->setValue(llformat("%.5f", mCenter.mV[VZ]));

	std::list<LLSD *>::iterator iter= mLLSDPrimData.begin();
	for(iter;iter!= mLLSDPrimData.end();iter++)
	{	
		LLSD *plsd=(*iter);

		if(plsd->emptyArray())
			continue;

		LLXMLNode *linkset_xml = new LLXMLNode("linkset", FALSE);

		for(LLSD::array_iterator link_itr = plsd->beginArray(); link_itr != plsd->endArray(); ++link_itr)
		{ 
			LLSD prim = (*link_itr);
				
			std::string selected_item	= "box";
			F32 scale_x=1.f, scale_y=1.f;
			
			LLVolumeParams volume_params;
			volume_params.fromLLSD(prim["volume"]);
			// Volume type
			U8 path = volume_params.getPathParams().getCurveType();
			U8 profile_and_hole = volume_params.getProfileParams().getCurveType();
			U8 profile	= profile_and_hole & LL_PCODE_PROFILE_MASK;
			U8 hole		= profile_and_hole & LL_PCODE_HOLE_MASK;
				
			// Scale goes first so we can differentiate between a sphere and a torus,
			// which have the same profile and path types.
			// Scale
			scale_x = volume_params.getRatioX();
			scale_y = volume_params.getRatioY();
			BOOL linear_path = (path == LL_PCODE_PATH_LINE) || (path == LL_PCODE_PATH_FLEXIBLE);
			if ( linear_path && profile == LL_PCODE_PROFILE_CIRCLE )
			{
				selected_item = "cylinder";
			}
			else if ( linear_path && profile == LL_PCODE_PROFILE_SQUARE )
			{
				selected_item = "box";
			}
			else if ( linear_path && profile == LL_PCODE_PROFILE_ISOTRI )
			{
				selected_item = "prism";
			}
			else if ( linear_path && profile == LL_PCODE_PROFILE_EQUALTRI )
			{
				selected_item = "prism";
			}
			else if ( linear_path && profile == LL_PCODE_PROFILE_RIGHTTRI )
			{
				selected_item = "prism";
			}
			else if (path == LL_PCODE_PATH_FLEXIBLE) 
			{
				selected_item = "cylinder"; 
			}
			else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_CIRCLE && scale_y > 0.75f)
			{
				selected_item = "sphere";
			}
			else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_CIRCLE && scale_y <= 0.75f)
			{
				selected_item = "torus";
			}
			else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_CIRCLE_HALF)
			{
				selected_item = "sphere";
			}
			else if ( path == LL_PCODE_PATH_CIRCLE2 && profile == LL_PCODE_PROFILE_CIRCLE )
			{
				selected_item = "sphere";
			}
			else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_EQUALTRI )
			{
				selected_item = "ring";
			}
			else if ( path == LL_PCODE_PATH_CIRCLE && profile == LL_PCODE_PROFILE_SQUARE && scale_y <= 0.75f)
			{
				selected_item = "tube";
			}
			else
			{
				llinfos << "Unknown path " << (S32) path << " profile " << (S32) profile << " in getState" << llendl;
				selected_item = "box";
				//continue;
			}

			// Create an LLSD object that represents this prim. It will be injected in to the overall LLSD
			// tree structure
			LLXMLNode *prim_xml;
			LLPCode pcode = prim["pcode"].asInteger();
			// Sculpt
			if (prim.has("sculpt"))
				prim_xml = new LLXMLNode("sculpt", FALSE);
			else if (pcode == LL_PCODE_LEGACY_GRASS)
			{
				prim_xml = new LLXMLNode("grass", FALSE);
				LLXMLNodePtr shadow_xml = prim_xml->createChild("type", FALSE);
				shadow_xml->createChild("val", TRUE)->setValue(prim["species"]);
			}
			else if (pcode == LL_PCODE_LEGACY_TREE)
			{
				prim_xml = new LLXMLNode("tree", FALSE);
				LLXMLNodePtr shadow_xml = prim_xml->createChild("type", FALSE);
				shadow_xml->createChild("val", TRUE)->setValue(prim["species"]);
			}
			else
				prim_xml = new LLXMLNode(selected_item.c_str(), FALSE);
			
			//Properties
			LLSD props=prim["properties"];
			prim_xml->createChild("name", FALSE)->setValue("<![CDATA[" + std::string(props["name"]) + "]]>");
			prim_xml->createChild("description", FALSE)->setValue("<![CDATA[" + std::string(props["description"]) + "]]>");
						
			// Transforms		
			LLXMLNodePtr position_xml = prim_xml->createChild("position", FALSE);
			LLVector3 position;
			position.setVec(ll_vector3d_from_sd(prim["position"]));
			position_xml->createChild("x", TRUE)->setValue(llformat("%.5f", position.mV[VX]));
			position_xml->createChild("y", TRUE)->setValue(llformat("%.5f", position.mV[VY]));
			position_xml->createChild("z", TRUE)->setValue(llformat("%.5f", position.mV[VZ]));
			LLXMLNodePtr scale_xml = prim_xml->createChild("size", FALSE);
			LLVector3 scale = ll_vector3_from_sd(prim["scale"]);
			scale_xml->createChild("x", TRUE)->setValue(llformat("%.5f", scale.mV[VX]));
			scale_xml->createChild("y", TRUE)->setValue(llformat("%.5f", scale.mV[VY]));
			scale_xml->createChild("z", TRUE)->setValue(llformat("%.5f", scale.mV[VZ]));
			LLXMLNodePtr rotation_xml = prim_xml->createChild("rotation", FALSE);
			LLQuaternion rotation = ll_quaternion_from_sd(prim["rotation"]);
			rotation_xml->createChild("x", TRUE)->setValue(llformat("%.5f", rotation.mQ[VX]));
			rotation_xml->createChild("y", TRUE)->setValue(llformat("%.5f", rotation.mQ[VY]));
			rotation_xml->createChild("z", TRUE)->setValue(llformat("%.5f", rotation.mQ[VZ]));
			rotation_xml->createChild("w", TRUE)->setValue(llformat("%.5f", rotation.mQ[VW]));
			
			// Flags
			if(prim["phantom"].asBoolean())
			{
				LLXMLNodePtr shadow_xml = prim_xml->createChild("phantom", FALSE);
				shadow_xml->createChild("val", TRUE)->setValue("true");
			}
			if(prim["physical"].asBoolean())
			{
				LLXMLNodePtr shadow_xml = prim_xml->createChild("physical", FALSE);
				shadow_xml->createChild("val", TRUE)->setValue("true");
			}
			
			// Grab S path
			F32 begin_s	= volume_params.getBeginS();
			F32 end_s	= volume_params.getEndS();
			
			// Compute cut and advanced cut from S and T
			F32 begin_t = volume_params.getBeginT();
			F32 end_t	= volume_params.getEndT();
			
			// Hollowness
			F32 hollow = volume_params.getHollow();
			
			// Twist
			F32 twist		= volume_params.getTwist() * 180.0;
			F32 twist_begin = volume_params.getTwistBegin() * 180.0;

			// Cut interpretation varies based on base object type
			F32 cut_begin, cut_end, adv_cut_begin, adv_cut_end;
			if ( selected_item == "sphere" || selected_item == "torus" || 
				 selected_item == "tube"   || selected_item == "ring" )
			{
				cut_begin		= begin_t;
				cut_end			= end_t;
				adv_cut_begin	= begin_s;
				adv_cut_end		= end_s;
			}
			else
			{
				cut_begin       = begin_s;
				cut_end         = end_s;
				adv_cut_begin   = begin_t;
				adv_cut_end     = end_t;
			}
			if (selected_item != "sphere")
			{		
				// Shear
				F32 shear_x = volume_params.getShearX();
				F32 shear_y = volume_params.getShearY();
				LLXMLNodePtr shear_xml = prim_xml->createChild("top_shear", FALSE);
				shear_xml->createChild("x", TRUE)->setValue(llformat("%.5f", shear_x));
				shear_xml->createChild("y", TRUE)->setValue(llformat("%.5f", shear_y));
				
			
			}
			if (selected_item == "box" || selected_item == "cylinder" || selected_item == "prism")
			{
				// Taper
				F32 taper_x = 1.f - volume_params.getRatioX();
				F32 taper_y = 1.f - volume_params.getRatioY();
				LLXMLNodePtr taper_xml = prim_xml->createChild("taper", FALSE);
				taper_xml->createChild("x", TRUE)->setValue(llformat("%.5f", taper_x));
				taper_xml->createChild("y", TRUE)->setValue(llformat("%.5f", taper_y));
			}
			else if (selected_item == "torus" || selected_item == "tube" || selected_item == "ring")
			{
				// Taper
				F32 taper_x	= volume_params.getTaperX();
				F32 taper_y = volume_params.getTaperY();
				LLXMLNodePtr taper_xml = prim_xml->createChild("taper", FALSE);
				taper_xml->createChild("x", TRUE)->setValue(llformat("%.5f", taper_x));
				taper_xml->createChild("y", TRUE)->setValue(llformat("%.5f", taper_y));
				
				//Hole Size
				F32 hole_size_x = volume_params.getRatioX();
				F32 hole_size_y = volume_params.getRatioY();
				LLXMLNodePtr hole_size_xml = prim_xml->createChild("hole_size", FALSE);
				hole_size_xml->createChild("x", TRUE)->setValue(llformat("%.5f", hole_size_x));
				hole_size_xml->createChild("y", TRUE)->setValue(llformat("%.5f", hole_size_y));
				
				//Advanced cut
				LLXMLNodePtr profile_cut_xml = prim_xml->createChild("profile_cut", FALSE);
				profile_cut_xml->createChild("begin", TRUE)->setValue(llformat("%.5f", adv_cut_begin));
				profile_cut_xml->createChild("end", TRUE)->setValue(llformat("%.5f", adv_cut_end));
				
				//Skew
				F32 skew = volume_params.getSkew();
				LLXMLNodePtr skew_xml = prim_xml->createChild("skew", FALSE);
				skew_xml->createChild("val", TRUE)->setValue(llformat("%.5f", skew));
				
				//Radius offset
				F32 radius_offset = volume_params.getRadiusOffset();
				LLXMLNodePtr radius_offset_xml = prim_xml->createChild("radius_offset", FALSE);
				radius_offset_xml->createChild("val", TRUE)->setValue(llformat("%.5f", radius_offset));
				
				// Revolutions
				F32 revolutions = volume_params.getRevolutions();
				LLXMLNodePtr revolutions_xml = prim_xml->createChild("revolutions", FALSE);
				revolutions_xml->createChild("val", TRUE)->setValue(llformat("%.5f", revolutions));
			}

			LLXMLNodePtr path_cut_xml = prim_xml->createChild("path_cut", FALSE);
			path_cut_xml->createChild("begin", TRUE)->setValue(llformat("%.5f", cut_begin));
			path_cut_xml->createChild("end", TRUE)->setValue(llformat("%.5f", cut_end));

			LLXMLNodePtr twist_xml = prim_xml->createChild("twist", FALSE);
			twist_xml->createChild("begin", TRUE)->setValue(llformat("%.5f", twist_begin));
			twist_xml->createChild("end", TRUE)->setValue(llformat("%.5f", twist));

			// All hollow objects allow a shape to be selected.
			if (hollow > 0.f)
			{
				const char	*selected_hole	= "1";
				switch (hole)
				{
				case LL_PCODE_HOLE_CIRCLE:
					selected_hole = "3";
					break;
				case LL_PCODE_HOLE_SQUARE:
					selected_hole = "2";
					break;
				case LL_PCODE_HOLE_TRIANGLE:
					selected_hole = "4";
					break;
				case LL_PCODE_HOLE_SAME:
				default:
					selected_hole = "1";
					break;
				}

				LLXMLNodePtr hollow_xml = prim_xml->createChild("hollow", FALSE);
				hollow_xml->createChild("amount", TRUE)->setValue(llformat("%.5f", hollow * 100.0));
				hollow_xml->createChild("shape", TRUE)->setValue(llformat("%s", selected_hole));
			}

			// Flexible
			if(prim.has("flexible"))
			{
				LLFlexibleObjectData attributes;
				attributes.fromLLSD(prim["flexible"]);

				LLXMLNodePtr flex_xml = prim_xml->createChild("flexible", FALSE);

				LLXMLNodePtr softness_xml = flex_xml->createChild("softness", FALSE);
				softness_xml->createChild("val", TRUE)->setValue(llformat("%.5f", (F32)attributes.getSimulateLOD()));

				LLXMLNodePtr gravity_xml = flex_xml->createChild("gravity", FALSE);
				gravity_xml->createChild("val", TRUE)->setValue(llformat("%.5f", attributes.getGravity()));

				LLXMLNodePtr drag_xml = flex_xml->createChild("drag", FALSE);
				drag_xml->createChild("val", TRUE)->setValue(llformat("%.5f", attributes.getAirFriction()));

				LLXMLNodePtr wind_xml = flex_xml->createChild("wind", FALSE);
				wind_xml->createChild("val", TRUE)->setValue(llformat("%.5f", attributes.getWindSensitivity()));
	
				LLXMLNodePtr tension_xml = flex_xml->createChild("tension", FALSE);
				tension_xml->createChild("val", TRUE)->setValue(llformat("%.5f", attributes.getTension()));

				LLXMLNodePtr force_xml = flex_xml->createChild("force", FALSE);
				force_xml->createChild("x", TRUE)->setValue(llformat("%.5f", attributes.getUserForce().mV[VX]));
				force_xml->createChild("y", TRUE)->setValue(llformat("%.5f", attributes.getUserForce().mV[VY]));
				force_xml->createChild("z", TRUE)->setValue(llformat("%.5f", attributes.getUserForce().mV[VZ]));
			}
			
			// Light
			if (prim.has("light"))
			{
				LLLightParams light;
				light.fromLLSD(prim["light"]);

				LLXMLNodePtr light_xml = prim_xml->createChild("light", FALSE);

				LLXMLNodePtr color_xml = light_xml->createChild("color", FALSE);
				LLColor4 color = light.getColor();
				color_xml->createChild("r", TRUE)->setValue(llformat("%u", (U32)(color.mV[VRED] * 255)));
				color_xml->createChild("g", TRUE)->setValue(llformat("%u", (U32)(color.mV[VGREEN] * 255)));
				color_xml->createChild("b", TRUE)->setValue(llformat("%u", (U32)(color.mV[VBLUE] * 255)));
	
				LLXMLNodePtr intensity_xml = light_xml->createChild("intensity", FALSE);
				intensity_xml->createChild("val", TRUE)->setValue(llformat("%.5f", color.mV[VALPHA]));

				LLXMLNodePtr radius_xml = light_xml->createChild("radius", FALSE);
				radius_xml->createChild("val", TRUE)->setValue(llformat("%.5f", light.getRadius()));

				LLXMLNodePtr falloff_xml = light_xml->createChild("falloff", FALSE);
				falloff_xml->createChild("val", TRUE)->setValue(llformat("%.5f", light.getFalloff()));
			}

			// Sculpt
			if (prim.has("sculpt"))
			{
				LLSculptParams sculpt;
				sculpt.fromLLSD(prim["sculpt"]);
				
				LLXMLNodePtr topology_xml = prim_xml->createChild("topology", FALSE);
				topology_xml->createChild("val", TRUE)->setValue(llformat("%u", sculpt.getSculptType()));
				
				std::string sculpttexture;
				sculpt.getSculptTexture().toString(sculpttexture);
				prim_xml->createChild("sculptmap_file", FALSE)->setValue(sculpttexture+".tga");
				prim_xml->createChild("sculptmap_uuid", FALSE)->setValue(sculpttexture);
			}
			LLXMLNodePtr texture_xml = prim_xml->createChild("texture", FALSE);

			// Textures
			LLSD te_llsd;
			LLSD tes = prim["textures"];
			LLPrimitive object;
			object.setNumTEs(U8(tes.size()));
			
			for (int i = 0; i < tes.size(); i++)
			{
				LLTextureEntry tex;
				tex.fromLLSD(tes[i]);
				object.setTE(U8(i), tex);
			}
		
			for (int i = 0; i < tes.size(); i++)
			{
				LLTextureEntry tex;
				tex.fromLLSD(tes[i]);
				std::list<LLUUID>::iterator iter;
				
				LLXMLNodePtr face_xml = texture_xml->createChild("face", FALSE);
				//This may be a hack, but it's ok since we're not using id in this code. We set id differently because for whatever reason
				//llxmlnode filters a few parameters including ID. -Patrick Sapinski (Friday, September 25, 2009)
				face_xml->mID = llformat("%d", i);

				LLXMLNodePtr tile_xml = face_xml->createChild("tile", FALSE);
				tile_xml->createChild("u", TRUE)->setValue(llformat("%.5f", object.getTE(i)->mScaleS));
				tile_xml->createChild("v", TRUE)->setValue(llformat("%.5f", object.getTE(i)->mScaleT));

				LLXMLNodePtr offset_xml = face_xml->createChild("offset", FALSE);
				offset_xml->createChild("u", TRUE)->setValue(llformat("%.5f", object.getTE(i)->mOffsetS));
				offset_xml->createChild("v", TRUE)->setValue(llformat("%.5f", object.getTE(i)->mOffsetT));

				LLXMLNodePtr rotation_xml = face_xml->createChild("rotation", FALSE);
				rotation_xml->createChild("w", TRUE)->setValue(llformat("%.5f", (object.getTE(i)->mRotation * RAD_TO_DEG)));

				LLUUID texture = object.getTE(i)->getID();
				std::string uuid_string;
				object.getTE(i)->getID().toString(uuid_string);
				
				face_xml->createChild("image_file", FALSE)->setValue("<![CDATA[" + uuid_string + ".tga]]>");
				face_xml->createChild("image_uuid", FALSE)->setValue(uuid_string);

				LLXMLNodePtr color_xml = face_xml->createChild("color", FALSE);
				LLColor4 color = object.getTE(i)->getColor();
				color_xml->createChild("r", TRUE)->setValue(llformat("%u", (int)(color.mV[VRED] * 255.f)));
				color_xml->createChild("g", TRUE)->setValue(llformat("%u", (int)(color.mV[VGREEN] * 255.f)));
				color_xml->createChild("b", TRUE)->setValue(llformat("%u", (int)(color.mV[VBLUE] * 255.f)));

				LLXMLNodePtr transparency_xml = face_xml->createChild("transparency", FALSE);
				transparency_xml->createChild("val", TRUE)->setValue(llformat("%u", (int)((1.f - color.mV[VALPHA]) * 100.f)));

				LLXMLNodePtr glow_xml = face_xml->createChild("glow", FALSE);
				glow_xml->createChild("val", TRUE)->setValue(llformat("%.5f", object.getTE(i)->getGlow()));

				if(object.getTE(i)->getFullbright() || object.getTE(i)->getShiny() || object.getTE(i)->getBumpmap())
				{
					std::string temp = "false";
					if(object.getTE(i)->getFullbright())
						temp = "true";
					LLXMLNodePtr fullbright_xml = face_xml->createChild("fullbright", FALSE);
					fullbright_xml->createChild("val", TRUE)->setValue(temp);
					LLXMLNodePtr shine_xml = face_xml->createChild("shine", FALSE);
					shine_xml->createChild("val", TRUE)->setValue(llformat("%u",object.getTE(i)->getShiny()));
					LLXMLNodePtr bumpmap_xml = face_xml->createChild("bump", FALSE);
					bumpmap_xml->createChild("val", TRUE)->setValue(llformat("%u",object.getTE(i)->getBumpmap()));
				}
			
			} // end for each texture
			
			LLXMLNodePtr inventory_xml = prim_xml->createChild("inventory", FALSE);
		
			LLSD inventory = prim["inventory"];

			//for each inventory item
			for (LLSD::array_iterator inv = (inventory).beginArray(); inv != (inventory).endArray(); ++inv)
			{
				LLSD item = (*inv);
				//<item>
				LLXMLNodePtr field_xml = inventory_xml->createChild("item", FALSE);	
				field_xml->createChild("description", FALSE)->setValue(item["desc"].asString());
				field_xml->createChild("item_id", FALSE)->setValue(item["item_id"].asString());
				field_xml->createChild("name", FALSE)->setValue(item["name"].asString());
				field_xml->createChild("type", FALSE)->setValue(item["type"].asString());
			} // end for each inventory item

			//add this prim to the linkset.
			linkset_xml->addChild(prim_xml);
		} //end for each object

		//add this linkset to the group.
		group_xml->addChild(linkset_xml);
		delete plsd;
	} 

	// Create a file stream and write to it
	llofstream out(mFileName,std::ios_base::out);
	if (!out.good())
	{
		llwarns << "Unable to open for output." << llendl;
	}
	else
	{
		out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
		project_xml->addChild(group_xml);
		project_xml->writeToOstream(out);
		out.close();
	}
}