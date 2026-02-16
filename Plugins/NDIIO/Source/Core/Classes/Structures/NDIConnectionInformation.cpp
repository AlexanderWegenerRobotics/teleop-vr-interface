/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Structures/NDIConnectionInformation.h>

#include <string>

/** Copies an existing instance to this object */
FNDIConnectionInformation::FNDIConnectionInformation(const FNDIConnectionInformation& other)
{
	// perform a deep copy of the 'other' structure and store the values in this object
	this->Bandwidth = other.Bandwidth;
	this->MachineName = other.MachineName;
	this->SourceName = other.SourceName;
	this->StreamName = other.StreamName;
	this->Url = other.Url;
	this->bMuteAudio = other.bMuteAudio;
	this->bMuteVideo = other.bMuteVideo;
}

/** Copies existing instance properties to this object */
FNDIConnectionInformation& FNDIConnectionInformation::operator=(const FNDIConnectionInformation& other)
{
	// perform a deep copy of the 'other' structure
	this->Bandwidth = other.Bandwidth;
	this->MachineName = other.MachineName;
	this->SourceName = other.SourceName;
	this->StreamName = other.StreamName;
	this->Url = other.Url;
	this->bMuteAudio = other.bMuteAudio;
	this->bMuteVideo = other.bMuteVideo;

	// return the result of the copy
	return *this;
}

/** Compares this object to 'other' and returns a determination of whether they are equal */
bool FNDIConnectionInformation::operator==(const FNDIConnectionInformation& other) const
{
	// return the value of a deep compare against the 'other' structure
	return this->Bandwidth == other.Bandwidth &&
	       this->MachineName == other.MachineName && this->SourceName == other.SourceName &&
	       this->StreamName == other.StreamName && this->Url == other.Url &&
	       this->bMuteAudio == other.bMuteAudio && this->bMuteVideo == other.bMuteVideo;
}

FNDIConnectionInformation::operator NDIlib_recv_bandwidth_e() const
{
	return this->Bandwidth == ENDISourceBandwidth::MetadataOnly ? NDIlib_recv_bandwidth_metadata_only
		 : this->Bandwidth == ENDISourceBandwidth::AudioOnly    ? NDIlib_recv_bandwidth_audio_only
		 : this->Bandwidth == ENDISourceBandwidth::Lowest       ? NDIlib_recv_bandwidth_lowest
		 : NDIlib_recv_bandwidth_highest;
}

/** Resets the current parameters to the default property values */
void FNDIConnectionInformation::Reset()
{
	// Ensure we reset all the properties of this object to nominal default properties
	this->Bandwidth = ENDISourceBandwidth::Highest;
	this->MachineName = FString("");
	this->SourceName = FString("");
	this->StreamName = FString("");
	this->Url = FString("");
	this->bMuteAudio = false;
	this->bMuteVideo = false;
}

/** Attempts to serialize this object using an Archive object */
FArchive& FNDIConnectionInformation::Serialize(FArchive& Ar)
{
	// we want to make sure that we are able to serialize this object, over many different version of this structure
	int32 current_version = 0;

	// serialize this structure
	return Ar << current_version << this->Bandwidth
	          << this->MachineName << this->SourceName << this->StreamName << this->Url
	          << this->bMuteAudio << this->bMuteVideo;
}

/** Determines whether this object is valid connection information */
bool FNDIConnectionInformation::IsValid() const
{
	// Need at least a source name and/or machine+stream name and/or a URL
	return (!this->SourceName.IsEmpty()) ||
	       ((!this->MachineName.IsEmpty()) && (!this->StreamName.IsEmpty())) ||
	       (!this->Url.IsEmpty());
}

FString FNDIConnectionInformation::GetNDIName() const
{
	std::string source_name;

	if(!this->SourceName.IsEmpty())
		return this->SourceName;

	if ((!this->MachineName.IsEmpty()) && (!this->StreamName.IsEmpty()))
		return this->MachineName + " (" + this->StreamName + ")";

	return FString();
}


/** Compares this object to 'other" and returns a determination of whether they are NOT equal */
bool FNDIConnectionInformation::operator!=(const FNDIConnectionInformation& other) const
{
	return !(*this == other);
}
