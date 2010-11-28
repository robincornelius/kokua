#ifndef LL_RCPRIMBACKUP_H
#define LL_RCPRIMBACKUP_H

#include "llvoinventorylistener.h"
#include "llselectmgr.h" 
#include "llinventorymodel.h"
#include "llviewertexture.h"

class assetRequest_t
{
public:
	assetRequest_t() {};
	~assetRequest_t() {};
	std::string name;
	LLUUID id;
	LLUUID mAssetID;
	int mNoAttempts;
	time_t mLastAttempt;
	LLAssetType::EType type;
	LLPermissions perm;
	LLViewerObject * container;
};

class rcprimbackup: public LLSingleton<rcprimbackup>, LLVOInventoryListener
{
	public:
		rcprimbackup();
		~rcprimbackup();

		void resetAll();

		void exportSelectedObjects();
		static bool check_perms( LLSelectNode* node );
		void processObjectProperties(LLMessageSystem* msg, void** user_data);
		void gotPrimProperties(LLSelectNode * node,LLUUID id, bool needDelete);

		int mPropRequestQueue;
		int mInvRequestQueue;
		int mActiveAssetRequests;

		void inventoryChanged(LLViewerObject* obj,
										LLInventoryObject::object_list_t* inv,
										S32,
										void* q_id);

		bool canexportasset(LLPermissions perm,LLAssetType::EType type);

		bool queueForDownload(LLInventoryObject* item, LLViewerObject* container, std::string iname);
		bool downloadAsset(assetRequest_t * request);
		static void AssetRecievedCallback(LLVFS *vfs, const LLUUID& uuid, LLAssetType::EType type, void *userdata, S32 result, LLExtStat extstat);

		static void onFileLoadedForSave(BOOL success, 
					   LLViewerFetchedTexture *src_vi,
					   LLImageRaw* src, 
					   LLImageRaw* aux_src, 
					   S32 discard_level,
					   BOOL final,
					   void* userdata);

		
		void addToTexturesQueue(LLUUID id);
		void LinkSetToLLSD(LLPointer<LLViewerObject> obj);
		bool PrimToLLSD(LLPointer<LLViewerObject> obj,bool parent,LLSD &prim_llsd);

		void saveLLSD();
		void saveHPA();

		bool mSaved;

		int getExportCount() { return mObjectsToExport.size(); };
		int getReceievedPropertiesCount() { return recieved_properties_map.size(); };
		int getReceievedInventoryCount() { return recieved_inventory_map.size(); };

		int mTotalToExport;

		int mTotalTextures;
		int mTexturesRecieved;

		int mTotalAssets;
		int mAssetsRecieved;
	
		int mXmlExported;

		bool mExportTextures;
		bool mExportInventories;

		bool mIsFinished;

		bool mSlGrid;

	private:
		
		std::string mFileName;
		std::string mFolderName;

		static void exportworker(void *userdata);

		std::set <LLPointer <LLViewerObject> > mObjectsToExport;
		std::set <LLPointer <LLViewerObject> > mRootObjectsToExport;
		std::vector <LLPointer <LLViewerObject> > mPropertiesWaitQueue;
		std::vector <LLPointer <LLViewerObject> > mInventoryWaitQueue;
		std::set <LLUUID> mTexturesQueue;
		std::vector <assetRequest_t *> mAssetWaitQueue;
		std::list<LLSD *> mLLSDPrimData;
		std::map <LLUUID,LLSD *> recieved_properties_map;
		std::map <LLUUID,LLSD *> recieved_inventory_map;

		void requestPrimProperties(U32 localID);
		void requestPrimInventory(LLViewerObject * obj);

		void populateTexturesQueue();

		LLBBox mBoundingBox;
		LLVector3 mCenter;

		LLInventoryModel::item_array_t* mTextureInvItems;

		LLLoadedCallbackEntry::source_callback_list_t mCallbackTextureList;

	
};

#endif