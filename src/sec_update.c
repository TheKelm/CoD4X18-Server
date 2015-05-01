/*
===========================================================================
    Copyright (C) 2010-2013  Ninja and TheKelm

    This file is part of CoD4X17a-Server source code.

    CoD4X17a-Server source code is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    CoD4X17a-Server source code is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>
===========================================================================
*/
#include "q_shared.h"
#include "qcommon.h"
#include "sec_update.h"
#include "sec_crypto.h"
#include "sec_common.h"
#include "sec_sign.h"
#include "sys_main.h"
#include "msg.h"
#include "sys_net.h"
#include "netchan.h"
#include "filesystem.h"
#include "cmd.h"
#include "cvar.h"
#include "sys_cod4defs.h"
#include "httpftp.h"

#include <unistd.h>
#include <errno.h>

#define BUFSIZE 10240

char *Sec_StrTok(char *str,char *tokens,int id){
    static char *mem[100] = {NULL};
    char *ptr,*ptr2;
    if(id<0||id>99||tokens==NULL)
	return NULL;
    if(str==NULL){
	if(mem[id]==NULL){
	    return NULL;
	}
	
	for(ptr=mem[id];*ptr != 0;++ptr){
	    //printf("---%c\n",*ptr);
	    for(ptr2=tokens;*ptr2!=0;++ptr2){
		//printf("------%c\n",*ptr2);
		if(*ptr == *ptr2){
		    //printf("DEBUG!!!!!!!! %p:\"%s\", %p:\"%s\",%p:\"%s\".\n\n",ptr,ptr,ptr2,ptr2,mem[id],mem[id]);
		    *ptr=0;
		    ptr2=mem[id];
		    mem[id]=ptr+1;
		    //printf("DEBUG!!!!!!!! %p:\"%s\", %p:\"%s\".\n\n",ptr,ptr,ptr2,ptr2);
		    //__asm__("int $3");
		    return ptr2;
		}
	    }
	}
	if(ptr!=mem[id]){
	    ptr = mem[id];
	}
	else
	    ptr = NULL;
	mem[id]=NULL;
	return ptr;
    }
    else{
    //printf("Debugging: \"%s\"\n",str);
	//mem[id]=str;
	for(ptr=str;*ptr != 0 && mem[id]==NULL;++ptr){
	    for(ptr2=tokens;*ptr2!=0 && mem[id]==NULL;++ptr2){
		if(*ptr != *ptr2){
		    mem[id]=ptr;
		}
	    }
	}
	if(mem[id] == NULL) return NULL;
	return Sec_StrTok(NULL,tokens,id); // BECAUSE I CAN.
    }	
}

void Sec_FreeFileStruct(sec_file_t *file){
    if(file->next != NULL)
	Sec_FreeFileStruct(file->next);
    Sec_Free(file);
}



qboolean Sec_VerifyFile(byte* data, int length, const char* hash)
{
	if(Sec_VerifyMemory(hash, data, length) == qfalse)
	{
		return qfalse;
	}
	return qtrue;
	
/*	
	
	unsigned long size;
	char outhash[129];
	
	size = sizeof(outhash);
	if(!Sec_HashMemory(SEC_HASH_SHA256, data, length, outhash, &size,qfalse))
	{
		return qfalse;
	}

	if(Q_strncmp(hash, outhash, size))
	{
		return qfalse;
	}
	return qtrue;
*/
}

int Sec_DownloadFile(const char* baseurl, sec_file_t *currFile)
{
	
		char buff[1024];
		int transret, len;
	    ftRequest_t* curfileobj;
		qboolean isvalid;
		byte* filebuf;
		
		//Does our file already exist?
		len = FS_SV_ReadFile(currFile->path, (void*)&filebuf);
		
		if(len > 0)
		{	//Is it already the file we need? Then don't download it.
			isvalid = Sec_VerifyFile(filebuf, len, currFile->hash);
			FS_FreeFile(filebuf);
			if(isvalid)
			{
				currFile->alreadyInstalled = qtrue;
				return 0;
			}
		}
		
		Com_sprintf(buff, sizeof(buff), SEC_UPDATE_DOWNLOAD(baseurl, currFile->path));
		
		curfileobj = FileDownloadRequest(buff);
		if(curfileobj == NULL)
		{
			return -1;	
		}

		Com_Printf("Downloading file: \"%s\"\n\n",currFile->name);

		do {
			transret = FileDownloadSendReceive( curfileobj );
			Com_Printf("%s", FileDownloadGenerateProgress( curfileobj ));
			usleep(20000);
		} while (transret == 0);
		
		Com_Printf("\n");

		if(transret < 0)
		{
			FileDownloadFreeRequest(curfileobj);
			return -1;
		}

		Q_strncpyz(buff,currFile->name, sizeof(buff));
		Q_strcat(buff, sizeof(buff),".new");

		if(curfileobj->code != 200){
			Com_PrintError("Downloading has failed! Error code: %d. Update aborted.\n", curfileobj->code);
			FileDownloadFreeRequest(curfileobj);
			return -1;
		}

		if(curfileobj->contentLength != currFile->size)
		{
			Com_PrintError("Downloading has failed! Downloaded file has a different size %d expected %d.\n", curfileobj->contentLength, currFile->size);
			FileDownloadFreeRequest(curfileobj);
			return -1;
		}

		isvalid = Sec_VerifyFile(curfileobj->recvmsg.data + curfileobj->headerLength, curfileobj->contentLength, currFile->hash);

		if(isvalid){
			Com_Printf("Successfully downloaded file \"%s\".\n", currFile->name);
		}else{
			FileDownloadFreeRequest(curfileobj);
			Com_PrintError("File \"%s\" is corrupt!\nUpdate aborted.\n",currFile->name);
			return -1;
		}
		
	
		len = FS_SV_BaseWriteFile(buff, curfileobj->recvmsg.data + curfileobj->headerLength, curfileobj->contentLength);
		if(len != curfileobj->contentLength){

			len = FS_SV_HomeWriteFile(buff, curfileobj->recvmsg.data + curfileobj->headerLength, curfileobj->contentLength);
			if(len != curfileobj->contentLength)
			{
				Com_PrintError("Opening \"%s\" for writing! Update aborted.\n",buff);
				FileDownloadFreeRequest(curfileobj);
				return -1;
			}
		}
		FileDownloadFreeRequest(curfileobj);
		
		return 1;
}

sec_file_t* Sec_ParseLine(const char* line)
{
	
	char* ptr, *ptr2;
	
	ptr = (char*)line;
	
	sec_file_t *currFile;
	
	currFile = Sec_GMalloc(sec_file_t,1);
	
	if(currFile == NULL)
	{
		return currFile;
	}
	
	Com_Memset(currFile,0,sizeof(sec_file_t));
	
	ptr2 = strchr(ptr,' ');
	
	if(ptr2 == NULL){
		Com_PrintWarning("Sec_Update: Corrupt data from update server. Update aborted.\nDebug:\"%s\"\n",ptr);
		Sec_FreeFileStruct(currFile);
		return NULL;
	}
	*ptr2++ = 0;
	
	Q_strncpyz(currFile->path,ptr,sizeof(currFile->path));
	
	ptr = ptr2;
	
	ptr2 = strchr(ptr,' ');
	
	if(ptr2 == NULL){
		Com_PrintWarning("Sec_Update: Corrupt data from update server. Update aborted.\nDebug:\"%s\"\n",ptr);
		Sec_FreeFileStruct(currFile);
		return NULL;
	}
	
	*ptr2++ = 0;
	
	if(!isInteger(ptr, 0)){
		Com_PrintWarning("Sec_Update: Corrupt data from update server - size is not a number. Update aborted.\nDebug:\"%s\"\n",ptr);
		Sec_FreeFileStruct(currFile);
		return NULL;
	}
	
	currFile->size = atoi(ptr);
	
	Q_strncpyz(currFile->hash,ptr2,sizeof(currFile->hash));
	Q_strncpyz(currFile->name,currFile->path, sizeof(currFile->name));
	
	return currFile;
}

qboolean Sec_MakeExecutable(sec_file_t *currFile)
{
	char buff[1024];
	char filepathbuf[1024];
	const char* testfile;

	Q_strncpyz(buff,currFile->name, sizeof(buff));
	Q_strcat(buff, sizeof(buff),".new");

	testfile = FS_SV_GetFilepath(buff, filepathbuf, sizeof(filepathbuf));
	if(testfile == NULL)
	{
		Com_PrintError("Can not find file %s\n", filepathbuf);
		Com_PrintError("Update has failed!\n");
		return qfalse;
	}

	Com_Printf("Set permissions for: %s\n", testfile);
	if(FS_SetPermissionsExec(testfile) == qfalse)
	{
		Com_PrintError("CRITICAL ERROR: failed to change mode of the file \"%s\"! Aborting, manual installation might be required.\n", testfile);
		return qfalse;
	}
	return qtrue;
}


qboolean Sec_RemoveOldBackupfiles(sec_file_t *currFile)
{
	char name1[1024];
	char filepathbuf[1024];
	const char* testfile;


	while(currFile != NULL)
	{
		if(currFile->alreadyInstalled)
		{
			currFile = currFile->next;
			continue;
		}
		

		Q_strncpyz(name1, currFile->name, sizeof(name1));

		Q_strcat(name1, sizeof(name1), ".old");

		Com_Printf("Removing backup file %s...\n", name1);

		testfile = FS_SV_GetFilepath(name1, filepathbuf, sizeof(filepathbuf));
		if(testfile != NULL)
		{ // Old file exists, back it up
			FS_SV_BaseRemove( name1 );
			FS_SV_HomeRemove( name1 );
			testfile = FS_SV_GetFilepath(name1, filepathbuf, sizeof(filepathbuf));
			if(testfile != NULL)
			{
				Com_PrintWarning("Couldn't remove backup file: %s\n", testfile);
				return qfalse;
			}
		}
		currFile = currFile->next;
	}
	return qtrue;
}

qboolean Sec_Backupfiles(sec_file_t *currFile)
{
	char name1[1024];
	char filepathbuf[1024];
	const char* testfile;
	const char* curfilename;

	while(currFile != NULL)
	{
		if(currFile->alreadyInstalled)
		{
			currFile = currFile->next;
			continue;
		}
		
		if(strstr(currFile->name, EXECUTABLE_NAME))
		{
			curfilename = Sys_ExeFile();
		}else{
			curfilename = currFile->name;
		}
		Q_strncpyz(name1, currFile->name, sizeof(name1));
		Q_strcat(name1, sizeof(name1), ".old");

		Com_Printf("Backing up file %s...\n", curfilename);

		// Check if an old file exists with this name
		testfile = FS_SV_GetFilepath(curfilename, filepathbuf, sizeof(filepathbuf));
		if(testfile != NULL)
		{// Old file exists, back it up
			FS_SV_Rename(curfilename, name1);
		}
		testfile = FS_SV_GetFilepath(curfilename, filepathbuf, sizeof(filepathbuf));
		// We couldn't back it up. Raise error
		if(testfile != NULL)
		{
			Com_PrintError("Couldn't backup file %s\n", curfilename);
			return qfalse;
		}
		currFile = currFile->next;
	}
	return qtrue;
}

void Sec_UndoBackup(sec_file_t *currFile)
{
	char name1[1024];
	char filepathbuf[1024];
	const char* testfile;
	const char* curfilename;

	while(currFile != NULL)
	{
		if(currFile->alreadyInstalled)
		{
			currFile = currFile->next;
			continue;
		}
		
		Com_Printf("Undo backup file %s...\n", currFile->name);

		Q_strncpyz(name1, currFile->name, sizeof(name1));
		Q_strcat(name1, sizeof(name1), ".old");

		if(strstr(currFile->name, EXECUTABLE_NAME))
		{
			curfilename = Sys_ExeFile();
		}else{
			curfilename = currFile->name;
		}

		// Check if an backupfile exists with this name
		testfile = FS_SV_GetFilepath(name1, filepathbuf, sizeof(filepathbuf));
		if(testfile != NULL)
		{// Backupfile exists, rename it back
			FS_SV_BaseRemove( curfilename );
			FS_SV_HomeRemove( curfilename );
			FS_SV_Rename(name1, curfilename);
		}
		currFile = currFile->next;
	}
}

qboolean Sec_InstallNewFiles(sec_file_t *currFile)
{
	char name1[1024];
	char filepathbuf[1024];
	const char* testfile;
	const char* curfilename;

	while(currFile != NULL)
	{
		if(currFile->alreadyInstalled)
		{
			currFile = currFile->next;
			continue;
		}
		Com_Printf("Install file %s...\n", currFile->name);

		Q_strncpyz(name1, currFile->name, sizeof(name1));
		Q_strcat(name1, sizeof(name1), ".new");

		if(strstr(currFile->name, EXECUTABLE_NAME))
		{
			curfilename = Sys_ExeFile();
		}else{
			curfilename = currFile->name;
		}

		FS_SV_Rename(name1, curfilename);
		testfile = FS_SV_GetFilepath(curfilename, filepathbuf, sizeof(filepathbuf));
		if(testfile == NULL)
		{
			Com_PrintError("Failed to rename file %s to %s\n", name1, curfilename);
			Com_PrintError("Update has failed!\n");
			return qfalse;
		}
		currFile = currFile->next;
	}
	return qtrue;
}



void Sec_Update( qboolean getbasefiles ){
    char buff[SEC_UPDATE_INITIALBUFFSIZE];
    char *ptr;
    char baseurl[1024];
    sec_file_t files, *currFile = &files;
    int len;
    ftRequest_t* filetransferobj;
    int transret;


    if(!Sec_Initialized()){
	return;
    }

    Com_Printf("\n-----------------------------\n");
    Com_Printf(" CoD4X Auto Update\n");
    Com_Printf(" Current version: %g\n", SEC_VERSION);
    Com_Printf(" Current subversion: %g\n", SYS_COMMONVERSION);
    Com_Printf(" Current build: %d\n", BUILD_NUMBER);
    Com_Printf("-----------------------------\n\n");

    if(getbasefiles == qtrue)
    {
        Com_sprintf(buff, sizeof(buff), UPDATE_SERVER_NAME "?mode=10&os=" OS_STRING "&ver=%g", 1.0);
    }else{
        Com_sprintf(buff, sizeof(buff), UPDATE_SERVER_NAME "?mode=10&os=" OS_STRING "&ver=%g", SYS_COMMONVERSION);
    }
    filetransferobj = FileDownloadRequest( buff );

    if(filetransferobj == NULL){
		return;
    }

	do {
		transret = FileDownloadSendReceive( filetransferobj );
		usleep(20000);
	} while (transret == 0);

    if(transret < 0)
	{
		FileDownloadFreeRequest(filetransferobj);
		return;
    }
    /* Need to catch errors */
 //   FS_WriteFile("tmp.txt", va("%d", status), 1);

    // TODO: Do something with the status?

//    FS_WriteFile("tmp2.txt", packet.header, packet.headerLength);
//    FS_WriteFile("tmp3.txt", packet.content, packet.contentLength);
    if(filetransferobj->code <= 0){
		Com_PrintError("Receiving data. Error code: %d.\n", filetransferobj->code);
		FileDownloadFreeRequest(filetransferobj);
		return;
    }
    if(filetransferobj->code == 204){
		Com_Printf("\nServer is up to date.\n\n");
		FileDownloadFreeRequest(filetransferobj);
		return;
    }
    else if(filetransferobj->code != 200){
		Com_PrintWarning("The update server's malfunction.\nStatus code: %d.\n", filetransferobj->code);
		FileDownloadFreeRequest(filetransferobj);
		return;
    }

    Com_Memset(&files, 0, sizeof(files));
	
	if(filetransferobj->contentLength >= sizeof(buff))
	{
		len = sizeof(buff) -1;
	}else{
		len = filetransferobj->contentLength;
	}
	
	memcpy(buff, filetransferobj->recvmsg.data + filetransferobj->headerLength, len);
	buff[len] = '\0';

        /* We need to parse filenames etc */
        ptr = Sec_StrTok(buff,"\n",42); // Yes, 42.

	if(ptr == NULL || Q_stricmpn("baseurl: ", ptr, 9))
	{
		Com_PrintWarning("Sec_Update: Corrupt data from update server. Update aborted.\n");
		FileDownloadFreeRequest(filetransferobj);
		return;
	}
	Q_strncpyz(baseurl, ptr +9, sizeof(baseurl));
	
	//Parsing update filelist + downloadloading/verifying files
	while((ptr = Sec_StrTok(NULL,"\n",42)) != NULL)
	{
		currFile->next = Sec_ParseLine(ptr);
		currFile = currFile->next;

		if(currFile == NULL)
		{
			Sec_FreeFileStruct(files.next);
			FileDownloadFreeRequest(filetransferobj);
			Com_PrintError("Update has failed\n");
			return;
		}

		switch(Sec_DownloadFile(baseurl, currFile))
		{
			case 0:
				continue;
			case 1:
				break;
			case -1:
			default:
				FileDownloadFreeRequest(filetransferobj);
				Sec_FreeFileStruct(files.next);
				Com_PrintError("Update has failed\n");
				return;
		}
		if(strstr(currFile->name, "run"))
		{
			if(Sec_MakeExecutable(currFile) == qfalse)
			{
				Sec_FreeFileStruct(files.next);
				Com_PrintError("Update has failed\n");
				return;
			}
		}
	}

	FileDownloadFreeRequest(filetransferobj);
	//Everything is downloaded and parsed
    Com_Printf("All files downloaded successfully. Applying update...\n");
	//Installing the update files...
    currFile = files.next;

	if(Sec_RemoveOldBackupfiles(files.next) == qfalse)
	{
		Com_PrintError("Update has failed\n");
		Sec_FreeFileStruct(files.next);
		return;
	}
	if(Sec_Backupfiles(files.next) == qfalse)
	{
		Com_PrintError("Update has failed. Trying to recover...\n");
		Sec_UndoBackup(files.next);
		Sec_FreeFileStruct(files.next);
		return;
	}
	if(Sec_InstallNewFiles(files.next) == qfalse)
	{
		Com_PrintError("Update has failed. Trying to recover...\n");
		Sec_UndoBackup(files.next);
	}

#if 0
    while(currFile != NULL)
	{
		if(currFile->alreadyInstalled)
		{
			currFile = currFile->next;
			continue;
		}
		
		Com_Printf("Updating file %s...\n", currFile->name);
		Q_strncpyz(name1, currFile->name, sizeof(name1));

		Q_strcat(name1, sizeof(name1), ".old");

		Q_strncpyz(name2, currFile->name, sizeof(name2));

		Q_strcat(name2, sizeof(name2), ".new");

		testfile = FS_SV_GetFilepath(name1, filepathbuf, sizeof(filepathbuf));
		if(testfile != NULL)
		{ // Old file exists, back it up
			FS_SV_BaseRemove( name1 );
			FS_SV_HomeRemove( name1 );
			testfile = FS_SV_GetFilepath(name1, filepathbuf, sizeof(filepathbuf));
			if(testfile != NULL)
			{
				Com_PrintWarning("Couldn't remove backup file: %s\n", testfile);
			}
			if(FS_SV_HomeFileExists(name1) == qtrue)
			{
				Com_PrintError("Couldn't remove backup file from fs_homepath: %s\n", name1);
			}
		}
		// Check if an old file exists with this name
		testfile = FS_SV_GetFilepath(currFile->name, filepathbuf, sizeof(filepathbuf));
		if(testfile != NULL)
		{ // Old file exists, back it up
			FS_SV_Rename(currFile->name, name1);
		}
		testfile = FS_SV_GetFilepath(currFile->name, filepathbuf, sizeof(filepathbuf));
		// We couldn't back it up. Now we try to just delete it.
		if(testfile != NULL)
		{
			FS_SV_BaseRemove( currFile->name );
			FS_SV_HomeRemove( currFile->name );
			testfile = FS_SV_GetFilepath( currFile->name, filepathbuf, sizeof(filepathbuf) );
			if(testfile != NULL)
			{
				Com_PrintWarning("Couldn't remove file: %s\n", testfile);
			}
			if(FS_SV_HomeFileExists(currFile->name) == qtrue)
			{
				Com_PrintError("Couldn't remove file from fs_homepath: %s\n", currFile->name);
				Com_PrintError("Update has failed!\n");
				return;
			}
		}

		if(Q_strncmp(currFile->name, EXECUTABLE_NAME) == 0){
			/* This is not the executable file */
			FS_SV_Rename(name2, currFile->name);
			testfile = FS_SV_GetFilepath(currFile->name, filepathbuf, sizeof(filepathbuf));
			if(testfile == NULL)
			{
				Com_PrintError("Failed to rename file %s to %s\n", name2,currFile->name);
				Com_PrintError("Update has failed!\n");
				return;
			}
			Com_Printf("Update on file %s successfully applied.\n",currFile->name);

		}else{
			/* This is the executable file */
			testfile = FS_SV_GetFilepath(name2, filepathbuf, sizeof(filepathbuf));
			if(testfile == NULL)
			{
				Com_PrintError("Can not find file %s\n", name2);
				Com_PrintError("Update has failed!\n");
				return;
			}
			FS_RenameOSPath(Sys_ExeFile(), va("%s.dead", Sys_ExeFile()));
			FS_RemoveOSPath(va("%s.dead", Sys_ExeFile()));
			FS_RemoveOSPath(Sys_ExeFile());
			if(FS_FileExistsOSPath(Sys_ExeFile()))
			{
				Com_PrintError("Failed to delete file %s\n", Sys_ExeFile());
				Com_PrintError("Update has failed!\n");
				return;
			}
			FS_RenameOSPath(testfile, Sys_ExeFile());
			if(!FS_FileExistsOSPath(Sys_ExeFile()))
			{
				Com_PrintError("Failed to rename file %s\n", testfile);
				Com_PrintError("Update has failed! Manual reinstallation of file %s is required. This server is now broken!\n", Sys_ExeFile());
				return;
			}
			Com_Printf("Update on file %s successfully applied.\n", Sys_ExeFile());
		}
		currFile = currFile->next;
    }
#endif

    //Try just in case we got a copy
    FS_SetPermissionsExec(Sys_ExeFile());

    Sec_FreeFileStruct(files.next);

    Com_Printf("Finalizing update...\n");

    Sys_Restart("System has been updated and will restart now.");

}