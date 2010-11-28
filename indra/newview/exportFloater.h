

#ifndef LLFLOATEREXPORT_H
#define LLFLOATEREXPORT_H

#include "llfloater.h"
#include "llprogressbar.h"
#include "llbutton.h"
#include "llselectmgr.h"
#include "lltextbox.h"
#include "llcheckboxctrl.h"

class exportFloater : public LLFloater
{
public:

	exportFloater(const LLSD& key);
	virtual ~exportFloater();
	virtual	BOOL postBuild();
	virtual void onClose(bool app_quitting);
	virtual void draw();
	virtual void onOpen(const LLSD  &key);

	static void show(LLUUID key);

	void onClickExport();
	void onClickAbort();

private:

	LLProgressBar * mProgressProperties;
	LLProgressBar * mProgressInventories;
	LLProgressBar * mProgressAssets;
	LLProgressBar * mProgressTextures;
	LLProgressBar * mProgressXML;

	LLTextBox * mPropertiesAmount;
	LLTextBox * mInventoriesAmount;
	LLTextBox * mAssetsAmount;
	LLTextBox * mTexturesAmount;
	LLTextBox * mXMLAmount;

	LLCheckBoxCtrl * mExportInventoryCtrl;
	LLCheckBoxCtrl * mExportTexturesCtrl;

	LLButton * mExportButton;
	LLButton * mAbortButton;

	LLObjectSelectionHandle mSelection;
};

#endif