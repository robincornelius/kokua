
#include "llviewerprecompiledheaders.h"

#include "llfloaterreg.h"

#include "exportFloater.h"
#include "rcprimbackup.h"


exportFloater::exportFloater(const LLSD& key)
	: LLFloater(key)
{
}

exportFloater::~exportFloater()
{
}

void exportFloater::onOpen(const LLSD & key)
{
	mSelection = LLSelectMgr::getInstance()->getEditSelection();
	mExportButton->setEnabled(true);
	mAbortButton->setEnabled(false);

	rcprimbackup::getInstance()->resetAll();

}


BOOL exportFloater::postBuild()
{

	mProgressProperties	= getChild<LLProgressBar>("properties_progress_bar");
	mProgressInventories	= getChild<LLProgressBar>("inventories_progress_bar");
	mProgressAssets	= getChild<LLProgressBar>("assets_progress_bar");
	mProgressTextures	= getChild<LLProgressBar>("textures_progress_bar");
	mProgressXML	= getChild<LLProgressBar>("xml_progress_bar");

	mExportButton = getChild<LLButton>("export_btn");
	mAbortButton = getChild<LLButton>("abort_btn");

	mExportButton->setClickedCallback(boost::bind(&exportFloater::onClickExport,this));
	mAbortButton->setClickedCallback(boost::bind(&exportFloater::onClickAbort,this));
	
	mPropertiesAmount = getChild<LLTextBox>("properties_amount");
	mInventoriesAmount = getChild<LLTextBox>("inventory_amount");
	mAssetsAmount = getChild<LLTextBox>("asset_amount");
	mTexturesAmount = getChild<LLTextBox>("textures_amount");
	mXMLAmount = getChild<LLTextBox>("xml_amount");

	mExportInventoryCtrl = getChild<LLCheckBoxCtrl>("export_contents");
	mExportTexturesCtrl = getChild<LLCheckBoxCtrl>("export_textures");

	//clean all the counters
	rcprimbackup::getInstance()->resetAll();

	return TRUE;
}

void exportFloater::draw()
{
	//Update the stats here

	if(rcprimbackup::getInstance()->mTotalToExport>0)
	{
		mProgressProperties->setPercent(100.0*((float)rcprimbackup::getInstance()->getReceievedPropertiesCount()/(float)rcprimbackup::getInstance()->mTotalToExport));
		mProgressInventories->setPercent(100.0*((float)rcprimbackup::getInstance()->getReceievedInventoryCount()/(float)rcprimbackup::getInstance()->mTotalToExport));
	}
	std::stringstream sstr;
	sstr << rcprimbackup::getInstance()->getReceievedPropertiesCount() << "/" << rcprimbackup::getInstance()->mTotalToExport;
	mPropertiesAmount->setText(sstr.str());
	sstr.str("");

	sstr << rcprimbackup::getInstance()->getReceievedInventoryCount() << "/" << rcprimbackup::getInstance()->mTotalToExport;
	mInventoriesAmount->setText(sstr.str());
	sstr.str("");

	if(rcprimbackup::getInstance()->mTotalAssets>0)
	{
		mProgressAssets->setPercent(100.0*(float)rcprimbackup::getInstance()->mAssetsRecieved/(float)rcprimbackup::getInstance()->mTotalAssets);
	}

	sstr << rcprimbackup::getInstance()->mAssetsRecieved << "/" << rcprimbackup::getInstance()->mTotalAssets;
	mAssetsAmount->setText(sstr.str());
	sstr.str("");


	if(rcprimbackup::getInstance()->mTotalTextures>0)
	{
		mProgressTextures->setPercent(100.0*(float)rcprimbackup::getInstance()->mTexturesRecieved/(float)rcprimbackup::getInstance()->mTotalTextures);
	}

	sstr << rcprimbackup::getInstance()->mTexturesRecieved<< "/"<< rcprimbackup::getInstance()->mTotalTextures;
	mTexturesAmount->setText(sstr.str());
	sstr.str("");

	if(rcprimbackup::getInstance()->mTotalToExport>0)
	{
		mProgressXML->setPercent(100.0*(float)rcprimbackup::getInstance()->mXmlExported/(float)rcprimbackup::getInstance()->mTotalToExport);
	}

	sstr << rcprimbackup::getInstance()->mXmlExported<<"/"<<rcprimbackup::getInstance()->mTotalToExport;
	mXMLAmount->setText(sstr.str());
	sstr.str("");

	if(rcprimbackup::getInstance()->mIsFinished)
	{
		mExportButton->setEnabled(true);
		mAbortButton->setEnabled(false);
		mExportInventoryCtrl->setEnabled(true);
		mExportTexturesCtrl->setEnabled(false);
	}

	LLFloater::draw();
}

void exportFloater::onClose(bool app_quitting)
{
	mSelection = NULL;
}


//static 
void exportFloater::show(LLUUID key)
{
	exportFloater *floater = LLFloaterReg::showTypedInstance<exportFloater>("export", LLSD(key));
	if (!floater)
		return;

}

void exportFloater::onClickExport()
{
	mExportButton->setEnabled(false);
	mAbortButton->setEnabled(true);
	
	rcprimbackup::getInstance()->mExportInventories = mExportInventoryCtrl->get();
	rcprimbackup::getInstance()->mExportTextures = mExportTexturesCtrl->get();

	mExportInventoryCtrl->setEnabled(false);
	mExportTexturesCtrl->setEnabled(false);
	rcprimbackup::getInstance()->exportSelectedObjects();
}

void exportFloater::onClickAbort()
{
	rcprimbackup::getInstance()->resetAll();
	mExportButton->setEnabled(true);
	mAbortButton->setEnabled(false);
	mExportInventoryCtrl->setEnabled(true);
	mExportTexturesCtrl->setEnabled(false);
}