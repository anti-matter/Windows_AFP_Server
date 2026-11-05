/*
 *	LAFPObject.h
 *	Created 09/25/2005
 *
 *	Copyright © 2005, Michael J. Conrad. All Rights Reserved.
 */

#ifndef __LAFPOBJECT__
#define __LAFPOBJECT__

#include "afp.h"
#include "afphelp.h"
#include "volumes.h"

#include "LAFPBuffer.h"

typedef struct
{
	uint16	dirOffspring;
	int32	dirOwnerID;
	int32	dirGroupID;
	uint32	dirRights;
}DirParms;

typedef struct
{
	uint32		dfLen;
	uint32		rfLen;
	int64		extDFLen;
	int64		extRFLen;
}FileParms;

typedef struct
{
	uint16		attributes;
	uint32		parID;
	uint32		createDate;
	uint32		modDate;
	uint32		backupDate;
	FINDER_INFO	fInfo;
	WCHAR		longName[MAX_AFP_PATH];
	uint32		fileDirNum;
	
	union
	{
		DirParms	dir;
		FileParms	file;
	}u;
}AFPFileDirParms;


class LAFPObject
{
public:
						LAFPObject(
							uint32		objectID,
							uint32		parID,
							LAFPObject*	parent,
							bool		isDirectory
						)
						{
							mObjectID		= objectID;
							mParentID		= parID;
							mParent			= parent;
							mIsDirectory	= isDirectory;
							
							mResOpen		= false;
							mDataOpen		= false;
							mHasCharMap		= false;
						}
						
					   ~LAFPObject() {}
		
	virtual uint32		GetID()							{ return mObjectID; }
	virtual void		SetID(uint32 inID)				{ mObjectID = inID; }
	
	virtual uint32		GetParentID()					{ return mParentID; }
	virtual void		SetParentID(uint32 inID)		{ mParentID = inID; }
	
	virtual LAFPObject*	GetParent()						{ return mParent;	}
	virtual void		SetParent(LAFPObject* inPar)	{ mParent = inPar;	}
		
	virtual bool		IsDirectory()					{ return(mIsDirectory);	}
	virtual void		SetIsDirectory(bool isDir)		{ mIsDirectory = isDir; }
	virtual bool		IsResOpen()						{ return(mResOpen);		}
	virtual void		SetResOpen(bool isOpen)			{ mResOpen = isOpen;	}
	virtual bool		IsDataOpen()					{ return(mDataOpen);	}
	virtual void		SetDataOpen(bool isOpen)		{ mDataOpen = isOpen;	}
	virtual bool		HasCharMap()					{ return(mHasCharMap);	}
	virtual void		SetHasCharMap(bool hasCM)		{ mHasCharMap = hasCM;	}

private:
	
	uint32				mObjectID;
	
	bool				mIsDirectory;
	bool				mResOpen;
	bool				mDataOpen;
	bool				mHasCharMap;
	
	WCHAR				mObjectName[MAX_AFP30_NAME];
	WCHAR				mPath[MAX_PATH];
	
	//
	//This is this objects parent directory. Many AFP calls require
	//the object to know this, caching this increases perf.
	//
	LAFPObject*			mParent;
	uint32				mParentID;
};

#endif //__LAFPOBJECT__