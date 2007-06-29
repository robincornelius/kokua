/** 
 * @file llviewerparcelmgr.h
 * @brief Viewer-side representation of owned land
 *
 * Copyright (c) 2002-$CurrentYear$, Linden Research, Inc.
 * $License$
 */

#ifndef LL_LLVIEWERPARCELMGR_H
#define LL_LLVIEWERPARCELMGR_H

#include "v3dmath.h"
#include "lldarray.h"
#include "llframetimer.h"
#include "llmemory.h"
#include "llviewerimage.h"

class LLUUID;
class LLMessageSystem;
class LLParcel;
class LLViewerRegion;

// Constants for sendLandOwner
//const U32 NO_NEIGHBOR_JOIN = 0x0;
//const U32 ALL_NEIGHBOR_JOIN = U32(  NORTH_MASK 
//							  | SOUTH_MASK 
//							  | EAST_MASK 
//							  | WEST_MASK);

const F32 PARCEL_POST_HEIGHT = 0.666f;
//const F32 PARCEL_POST_HEIGHT = 20.f;

// Specify the type of land transfer taking place
//enum ELandTransferType
//{
//	LTT_RELEASE_LAND = 0x1,
//	LTT_CLAIM_LAND = 0x2,
//	LTT_BUY_LAND = 0x4,
//	LTT_DEED_LAND = 0x8,
//	LTT_FOR_GROUP = 0x16
//};

// Base class for people who want to "observe" changes in the viewer
// parcel selection.
class LLParcelObserver
{
public:
	virtual ~LLParcelObserver() {};
	virtual void changed() = 0;
};

class LLParcelSelection : public LLRefCount
{
	friend class LLViewerParcelMgr;

protected:
	~LLParcelSelection();

public:
	LLParcelSelection(LLParcel* parcel);
	LLParcelSelection();

	// this can return NULL at any time, as parcel selection
	// might have been invalidated.
	LLParcel* getParcel() { return mParcel; }

	// Return the number of grid units that are owned by you within
	// the selection (computed by server).
	S32 getSelfCount() const { return mSelectedSelfCount; }

	// Returns area that will actually be claimed in meters squared.
	S32		getClaimableArea() const;
	bool	hasOthersSelected() const;

	// Does the selection have multiple land owners in it?
	BOOL	getMultipleOwners() const;

	// Is the entire parcel selected, or just a part?
	BOOL	getWholeParcelSelected() const;

protected:
	void setParcel(LLParcel* parcel) { mParcel = parcel; }

protected:

	LLParcel*	mParcel;
	BOOL		mSelectedMultipleOwners;
	BOOL		mWholeParcelSelected;
	S32			mSelectedSelfCount;
	S32			mSelectedOtherCount;
	S32			mSelectedPublicCount;
};

typedef LLHandle<LLParcelSelection> LLParcelSelectionHandle;

class LLViewerParcelMgr
{

public:
	LLViewerParcelMgr();
	~LLViewerParcelMgr();

	static void cleanupGlobals();

	BOOL	selectionEmpty() const;
	F32		getSelectionWidth() const	{ return F32(mEastNorth.mdV[VX] - mWestSouth.mdV[VX]); }
	F32		getSelectionHeight() const	{ return F32(mEastNorth.mdV[VY] - mWestSouth.mdV[VY]); }
	BOOL	getSelection(LLVector3d &min, LLVector3d &max) { min = mWestSouth; max = mEastNorth; return !selectionEmpty();}
	LLViewerRegion* getSelectionRegion();
	F32		getDwelling() const { return mSelectedDwell;}

	void	getDisplayInfo(S32* area, S32* claim, S32* rent, BOOL* for_sale, F32* dwell);

	void	getPrimInfo(S32 &sw_max, S32 &sw_total, S32 &max, S32 &total, S32 &owner, S32 &group, S32 &other, S32& selected, F32 &parcel_object_bonus, S32 &other_clean);

	// Returns selected area
	S32 getSelectedArea() const;

	void resetSegments(U8* segments);

	// write a rectangle's worth of line segments into the highlight array
	void writeHighlightSegments(F32 west, F32 south, F32 east, F32 north);

	// Write highlight segments from a packed bitmap of the appropriate
	// parcel.
	void writeSegmentsFromBitmap(U8* bitmap, U8* segments);

	void writeAgentParcelFromBitmap(U8* bitmap);

	// Select the collision parcel
	void selectCollisionParcel();

	// Select the parcel at a specific point
	LLHandle<LLParcelSelection> selectParcelAt(const LLVector3d& pos_global);

	// Take the current rectangle select, and select the parcel contained
	// within it.
	LLParcelSelectionHandle selectParcelInRectangle();

	// Select a piece of land
	LLParcelSelectionHandle selectLand(const LLVector3d &corner1, const LLVector3d &corner2, 
					   BOOL snap_to_parcel);

	// Clear the selection, and stop drawing the highlight.
	void	deselectLand();
	void	deselectUnused();

	void addObserver(LLParcelObserver* observer);
	void removeObserver(LLParcelObserver* observer);
	void notifyObservers();

	void setSelectionVisible(BOOL visible) { mRenderSelection = visible; }

	BOOL	isOwnedAt(const LLVector3d& pos_global) const;
	BOOL	isOwnedSelfAt(const LLVector3d& pos_global) const;
	BOOL	isOwnedOtherAt(const LLVector3d& pos_global) const;
	BOOL	isSoundLocal(const LLVector3d &pos_global) const;

	BOOL	canHearSound(const LLVector3d &pos_global) const;

	// Returns a reference counted pointer to current parcel selection.  
	// Selection does not change to reflect new selections made by user
	// Use this when implementing a task UI that refers to a specific
	// selection.
	LLParcelSelectionHandle getParcelSelection() const;

	// Returns a reference counted pointer to current parcel selection.
	// Pointer tracks whatever the user has currently selected.
	// Use this when implementing an inspector UI.
	// http://en.wikipedia.org/wiki/Inspector_window
	LLParcelSelectionHandle getFloatingParcelSelection() const;

	//LLParcel *getParcelSelection() const;
	LLParcel *getAgentParcel() const;

	BOOL	inAgentParcel(const LLVector3d &pos_global) const;

	// Returns a pointer only when it has valid data.
	LLParcel*	getHoverParcel() const;

	LLParcel*	getCollisionParcel() const;

	BOOL	agentCanTakeDamage() const;
	BOOL	agentCanFly() const;
	F32		agentDrawDistance() const;
	BOOL	agentCanBuild() const;

	F32		getHoverParcelWidth() const		
				{ return F32(mHoverEastNorth.mdV[VX] - mHoverWestSouth.mdV[VX]); }

	F32		getHoverParcelHeight() const
				{ return F32(mHoverEastNorth.mdV[VY] - mHoverWestSouth.mdV[VY]); }

	// UTILITIES
	void	render();
	void	renderParcelCollision();

	void	renderRect(	const LLVector3d &west_south_bottom, 
						const LLVector3d &east_north_top );
	void	renderOneSegment(F32 x1, F32 y1, F32 x2, F32 y2, F32 height, U8 direction, LLViewerRegion* regionp);
	void	renderHighlightSegments(const U8* segments, LLViewerRegion* regionp);
	void	renderCollisionSegments(U8* segments, BOOL use_pass, LLViewerRegion* regionp);

	void	sendParcelGodForceOwner(const LLUUID& owner_id);

	// make the selected parcel a content parcel. 
	void sendParcelGodForceToContent();

	// Pack information about this parcel and send it to the region
	// containing the southwest corner of the selection.
	// If want_reply_to_update, simulator will send back a ParcelProperties
	// message.
	void	sendParcelPropertiesUpdate(LLParcel* parcel);

	// Takes an Access List flag, like AL_ACCESS or AL_BAN
	void	sendParcelAccessListUpdate(U32 which);

	// Takes an Access List flag, like AL_ACCESS or AL_BAN
	void	sendParcelAccessListRequest(U32 flags);

	// Dwell is not part of the usual parcel update information because the
	// simulator doesn't actually know the per-parcel dwell.  Ack!  We have
	// to get it out of the database.
	void	sendParcelDwellRequest();

	// If the point is outside the current hover parcel, request more data
	void	requestHoverParcelProperties(const LLVector3d& pos_global);

	bool	canAgentBuyParcel(LLParcel*, bool forGroup) const;
	
//	void	startClaimLand(BOOL is_for_group = FALSE);
	void	startBuyLand(BOOL is_for_group = FALSE);
	void	startSellLand();
	void	startReleaseLand();
	void	startDivideLand();
	void	startJoinLand();
	void	startDeedLandToGroup();
	void reclaimParcel();

	void	buyPass();

	// Buying Land
	
	class ParcelBuyInfo;
	ParcelBuyInfo* setupParcelBuy(const LLUUID& agent_id,
								  const LLUUID& session_id,						 
								  const LLUUID& group_id,
								  BOOL is_group_owned,
								  BOOL is_claim,
								  BOOL remove_contribution);
		// callers responsibility to call deleteParcelBuy() on return value
	void sendParcelBuy(ParcelBuyInfo*);
	void deleteParcelBuy(ParcelBuyInfo*&);
					   
	void sendParcelDeed(const LLUUID& group_id);

	// Send the ParcelRelease message
	void sendParcelRelease();

	// accessors for mAgentParcel
	const char *getAgentParcelName() const;

	// Create a landmark at the "appropriate" location for the
	// currently selected parcel.
	// *NOTE: Taken out 2005-03-21. Phoenix.
	//void makeLandmarkAtSelection();

	static void processParcelOverlay(LLMessageSystem *msg, void **user_data);
	static void processParcelProperties(LLMessageSystem *msg, void **user_data);
	static void processParcelAccessListReply(LLMessageSystem *msg, void **user);
	static void processParcelDwellReply(LLMessageSystem *msg, void **user);

	void dump();

	// Whether or not the collision border around the parcel is there because
	// the agent is banned or not in the allowed group
	BOOL isCollisionBanned();

	static BOOL isParcelOwnedByAgent(const LLParcel* parcelp, U64 group_proxy_power);
	static BOOL isParcelModifiableByAgent(const LLParcel* parcelp, U64 group_proxy_power);

protected:
	static void releaseAlertCB(S32 option, void *data);

	// If the user is claiming land and the current selection 
	// borders a piece of land the user already owns, ask if he
	// wants to join this land to the other piece.
	//void	askJoinIfNecessary(ELandTransferType land_transfer_type);
	//static void joinAlertCB(S32 option, void* data);

	//void buyAskMoney(ELandTransferType land_transfer_type);

	// move land from current owner to it's group.
	void deedLandToGroup();

	static void claimAlertCB(S32 option, void* data);
	static void buyAlertCB(S32 option, void* data);
	static void deedAlertCB(S32 option, void*);

	static void callbackDivideLand(S32 option, void* data);
	static void callbackJoinLand(S32 option, void* data);

	//void	finishClaim(BOOL user_to_user_sale, U32 join);

private:
	BOOL						mSelected;

	LLParcel*					mCurrentParcel;			// selected parcel info
	LLParcelSelectionHandle		mCurrentParcelSelection;
	LLParcelSelectionHandle		mFloatingParcelSelection;
	S32							mRequestResult;		// result of last parcel request
	LLVector3d					mWestSouth;
	LLVector3d					mEastNorth;
	F32							mSelectedDwell;

	LLParcel					*mAgentParcel;		// info for parcel agent is in
	S32							mAgentParcelSequenceID;	// incrementing counter to suppress out of order updates

	LLParcel*					mHoverParcel;
	S32							mHoverRequestResult;
	LLVector3d					mHoverWestSouth;
	LLVector3d					mHoverEastNorth;

	LLDynamicArray<LLParcelObserver*> mObservers;

	// Array of pieces of parcel edges to potentially draw
	// Has (parcels_per_edge + 1) * (parcels_per_edge + 1) elements so
	// we can represent edges of the grid.
	// WEST_MASK = draw west edge
	// SOUTH_MASK = draw south edge
	S32							mParcelsPerEdge;
	U8*							mHighlightSegments;
	U8*							mAgentParcelOverlay;

	// Raw data buffer for unpacking parcel overlay chunks
	// Size = parcels_per_edge * parcels_per_edge / parcel_overlay_chunks
	static U8*					sPackedOverlay;

	// Watch for pending collisions with a parcel you can't access.
	// If it's coming, draw the parcel's boundaries.
	LLParcel*					mCollisionParcel;
	U8*							mCollisionSegments;
	BOOL						mRenderCollision; 
	BOOL						mRenderSelection;
	S32							mCollisionBanned;     
	LLFrameTimer				mCollisionTimer;
	LLUUID						mBlockedImageID;
	LLUUID						mPassImageID;
	LLPointer<LLViewerImage> 	mBlockedImage;
	LLPointer<LLViewerImage> 	mPassImage;

	// Media
	S32 						mMediaParcelId;
	U64 						mMediaRegionId;
};

extern LLViewerParcelMgr *gParcelMgr;

void sanitize_corners(const LLVector3d &corner1, const LLVector3d &corner2,
						LLVector3d &west_south_bottom, LLVector3d &east_north_top);

#endif
