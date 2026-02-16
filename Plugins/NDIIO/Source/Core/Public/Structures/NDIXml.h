/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>

#include <FastXml.h>

class NDIXmlElementParser
{
public:
	virtual ~NDIXmlElementParser()
	{}

	// Start parsing this element
	virtual bool ProcessOpen(const TCHAR* ElementName, const TCHAR* ElementData)
	{
		return true;
	}

	// Parse an attribute of this element
	virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue)
	{
		return true;
	}

	// Start parsing a sub-element
	virtual TSharedRef<NDIXmlElementParser>* ProcessElement(const TCHAR* ElementName, const TCHAR* ElementData)
	{
		return nullptr;
	}

	// Finish parsing this element
	virtual bool ProcessClose(const TCHAR* ElementName)
	{
		return true;
	}
};

class NDIXmlElementParser_null : public NDIXmlElementParser
{
public:
};


class NDIXmlParser : public IFastXmlCallback
{
public:
	virtual ~NDIXmlParser()
	{}


	void AddElementParser(FName ElementName, TSharedRef<NDIXmlElementParser> ElementParser)
	{
		ElementParsers.Add(ElementName, ElementParser);
	}

	virtual bool ProcessXmlDeclaration(const TCHAR* ElementData, int32 XmlFileLineNumber) override
	{
		return true;
	}

	virtual bool ProcessElement(const TCHAR* ElementName, const TCHAR* ElementData, int32 XmlFileLineNumber) override
	{
		if(ElementParserStack.Num() == 0)
		{
			TSharedRef<NDIXmlElementParser>* ParserPtr = ElementParsers.Find(ElementName);
			if(ParserPtr == nullptr)
				ParserPtr = &NullParser;

			ElementParserStack.Push(*ParserPtr);
			return (*ParserPtr)->ProcessOpen(ElementName, ElementData);
		}
		else
		{
			TSharedRef<NDIXmlElementParser>* ParserPtr = ElementParserStack.Last()->ProcessElement(ElementName, ElementData);
			if(ParserPtr == nullptr)
				ParserPtr = &NullParser;

			ElementParserStack.Push(*ParserPtr);
			return (*ParserPtr)->ProcessOpen(ElementName, ElementData);
		}

		//return false;
	}

	virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue) override
	{
		if(ElementParserStack.Num() == 0)
		{
			return true;
		}
		else
		{
			return ElementParserStack.Last()->ProcessAttribute(AttributeName, AttributeValue);
		}

		//return false;
	}

	virtual bool ProcessClose(const TCHAR* ElementName) override
	{
		if(ElementParserStack.Num() == 0)
		{
			return true;
		}
		else
		{
			auto Parser = ElementParserStack.Pop();
			return Parser->ProcessClose(ElementName);
		}

		//return false;
	}

	virtual bool ProcessComment(const TCHAR* Comment) override
	{
		return true;
	}

protected:
	TMap<FName, TSharedRef<NDIXmlElementParser> > ElementParsers;
	TArray<TSharedRef<NDIXmlElementParser> > ElementParserStack;

	TSharedRef<NDIXmlElementParser> NullParser { MakeShareable(new NDIXmlElementParser_null()) };
};
