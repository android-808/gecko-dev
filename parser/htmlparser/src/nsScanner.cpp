/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

//#define __INCREMENTAL 1

#define NS_IMPL_IDS
#include "nsScanner.h"
#include "nsDebug.h"
#include "nsIServiceManager.h"
#include "nsICharsetConverterManager.h"
#include "nsICharsetAlias.h"
#include "nsFileSpec.h"

static NS_DEFINE_CID(kCharsetConverterManagerCID, NS_ICHARSETCONVERTERMANAGER_CID);

const char* kBadHTMLText="<H3>Oops...</H3>You just tried to read a non-existent document: <BR>";
const char* kUnorderedStringError = "String argument must be ordered. Don't you read API's?";

#ifdef __INCREMENTAL
const int   kBufsize=1;
#else
const int   kBufsize=64;
#endif

MOZ_DECL_CTOR_COUNTER(nsScanner);

/**
 *  Use this constructor if you want i/o to be based on 
 *  a single string you hand in during construction.
 *  This short cut was added for Javascript.
 *
 *  @update  gess 5/12/98
 *  @param   aMode represents the parser mode (nav, other)
 *  @return  
 */
nsScanner::nsScanner(nsString& anHTMLString, const nsString& aCharset, nsCharsetSource aSource) : 
  mBuffer(anHTMLString), mFilename(""), mUnicodeXferBuf("")
{
  MOZ_COUNT_CTOR(nsScanner);

  mTotalRead=mBuffer.Length();
  mIncremental=PR_FALSE;
  mOwnsStream=PR_FALSE;
  mOffset=0;
  mMarkPos=0;
  mInputStream=0;
  mUnicodeDecoder = 0;
  mCharset = "";
  mCharsetSource = kCharsetUninitialized;
  SetDocumentCharset(aCharset, aSource);
}

/**
 *  Use this constructor if you want i/o to be based on strings 
 *  the scanner receives. If you pass a null filename, you
 *  can still provide data to the scanner via append.
 *
 *  @update  gess 5/12/98
 *  @param   aFilename --
 *  @return  
 */
nsScanner::nsScanner(nsString& aFilename,PRBool aCreateStream, const nsString& aCharset, nsCharsetSource aSource) : 
    mBuffer(""), mFilename(aFilename), mUnicodeXferBuf("")
{
  MOZ_COUNT_CTOR(nsScanner);

  mIncremental=PR_TRUE;
  mOffset=0;
  mMarkPos=0;
  mTotalRead=0;
  mOwnsStream=aCreateStream;
  mInputStream=0;
  if(aCreateStream) {
		mInputStream = new nsInputFileStream(nsFileSpec(aFilename));
  } //if
  mUnicodeDecoder = 0;
  mCharset = "";
  mCharsetSource = kCharsetUninitialized;
  SetDocumentCharset(aCharset, aSource);
}

/**
 *  Use this constructor if you want i/o to be stream based.
 *
 *  @update  gess 5/12/98
 *  @param   aStream --
 *  @param   assumeOwnership --
 *  @param   aFilename --
 *  @return  
 */
nsScanner::nsScanner(nsString& aFilename,nsInputStream& aStream,const nsString& aCharset, nsCharsetSource aSource) :
    mBuffer(""), mFilename(aFilename) , mUnicodeXferBuf("")
{  
  MOZ_COUNT_CTOR(nsScanner);

  mIncremental=PR_FALSE;
  mOffset=0;
  mMarkPos=0;
  mTotalRead=0;
  mOwnsStream=PR_FALSE;
  mInputStream=&aStream;
  mUnicodeDecoder = 0;
  mCharset = "";
  mCharsetSource = kCharsetUninitialized;
  SetDocumentCharset(aCharset, aSource);
}


nsresult nsScanner::SetDocumentCharset(const nsString& aCharset , nsCharsetSource aSource) {

  nsresult res = NS_OK;

  if( aSource < mCharsetSource) // priority is lower the the current one , just
    return res;

  nsICharsetAlias* calias = nsnull;
  res = nsServiceManager::GetService(kCharsetAliasCID,
                                       kICharsetAliasIID,
                                       (nsISupports**)&calias);

  NS_ASSERTION( nsnull != calias, "cannot find charset alias");
  nsAutoString charsetName = aCharset;
  if( NS_SUCCEEDED(res) && (nsnull != calias))
  {
    PRBool same = PR_FALSE;
    res = calias->Equals(aCharset, mCharset, &same);
    if(NS_SUCCEEDED(res) && same)
    {
      return NS_OK; // no difference, don't change it
    }
    // different, need to change it
    res = calias->GetPreferred(aCharset, charsetName);
    nsServiceManager::ReleaseService(kCharsetAliasCID, calias);

    if(NS_FAILED(res) && (kCharsetUninitialized == mCharsetSource) )
    {
       // failed - unknown alias , fallback to ISO-8859-1
      charsetName = "ISO-8859-1";
    }
    mCharset = charsetName;
    mCharsetSource = aSource;

    nsICharsetConverterManager * ccm = nsnull;
    res = nsServiceManager::GetService(kCharsetConverterManagerCID, 
                                       nsCOMTypeInfo<nsICharsetConverterManager>::GetIID(), 
                                       (nsISupports**)&ccm);
    if(NS_SUCCEEDED(res) && (nsnull != ccm))
    {
      nsIUnicodeDecoder * decoder = nsnull;
      res = ccm->GetUnicodeDecoder(&mCharset, &decoder);
      if(NS_SUCCEEDED(res) && (nsnull != decoder))
      {
         NS_IF_RELEASE(mUnicodeDecoder);

         mUnicodeDecoder = decoder;
      }    
      nsServiceManager::ReleaseService(kCharsetConverterManagerCID, ccm);
    }
  }
  return res;
}


/**
 *  default destructor
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  
 */
nsScanner::~nsScanner() {
    
  MOZ_COUNT_DTOR(nsScanner);

  if(mInputStream) {
    mInputStream->close();
    if(mOwnsStream)
      delete mInputStream;
  }
  mInputStream=0;
  NS_IF_RELEASE(mUnicodeDecoder);
}

/**
 *  Resets current offset position of input stream to marked position. 
 *  This allows us to back up to this point if the need should arise, 
 *  such as when tokenization gets interrupted.
 *  NOTE: IT IS REALLY BAD FORM TO CALL RELEASE WITHOUT CALLING MARK FIRST!
 *
 *  @update  gess 5/12/98
 *  @param   
 *  @return  
 */
PRUint32 nsScanner::RewindToMark(void){
  mOffset=mMarkPos;
  return mOffset;
}


/**
 *  Records current offset position in input stream. This allows us
 *  to back up to this point if the need should arise, such as when
 *  tokenization gets interrupted.
 *
 *  @update  gess 7/29/98
 *  @param   
 *  @return  
 */
PRUint32 nsScanner::Mark(PRInt32 anIndex){
  if(kNotFound==anIndex) {
    if((mOffset>0) && (mOffset>eBufferSizeThreshold)) {
      mBuffer.Cut(0,mOffset);   //delete chars up to mark position
      mOffset=0;
    }
    mMarkPos=mOffset;
  }
  else mOffset=(PRUint32)anIndex;
  return 0;
}
 

/** 
 * Append data to our underlying input buffer as
 * if it were read from an input stream.
 *
 * @update  gess4/3/98
 * @return  error code 
 */
PRBool nsScanner::Append(const nsString& aBuffer) {
  mBuffer.Append(aBuffer);
  mTotalRead+=aBuffer.Length();
  return PR_TRUE;
}

/**
 *  
 *  
 *  @update  gess 5/21/98
 *  @param   
 *  @return  
 */
PRBool nsScanner::Append(const char* aBuffer, PRUint32 aLen){
 
  if(mUnicodeDecoder) {
    PRInt32 unicharBufLen = 0;
    mUnicodeDecoder->GetMaxLength(aBuffer, aLen, &unicharBufLen);
    mUnicodeXferBuf.SetCapacity(unicharBufLen+32);
    mUnicodeXferBuf.Truncate();
    PRUnichar *unichars = (PRUnichar*)mUnicodeXferBuf.GetUnicode();
	  
    nsresult res;
	  do {
	    PRInt32 srcLength = aLen;
		  PRInt32 unicharLength = unicharBufLen;
		  res = mUnicodeDecoder->Convert(aBuffer, &srcLength, unichars, &unicharLength);
      unichars[unicharLength]=0;  //add this since the unicode converters can't be trusted to do so.


                  // Move the nsParser.cpp 00 -> space hack to here so
                  // it won't break UCS2 file
                  // Hack Start
                  for(PRInt32 i=0;i<unicharLength;i++)
                    NS_WARN_IF_FALSE(0x0000 != unichars[i], "found a null character");
                     //if(0x0000 == unichars[i])
                        //unichars[i] = 0x0020;
                  // Hack End

		  mBuffer.Append(unichars, unicharLength);
		  mTotalRead += unicharLength;
                  // if we failed, we consume one byte by replace it with U+FFFD
                  // and try conversion again.
		  if(NS_FAILED(res)) {
			  mUnicodeDecoder->Reset();
			  mBuffer.Append( (PRUnichar)0xFFFD);
			  mTotalRead++;
			  if(((PRUint32) (srcLength + 1)) > aLen)
				  srcLength = aLen;
			  else 
				  srcLength++;
			  aBuffer += srcLength;
			  aLen -= srcLength;
		  }
	  } while (NS_FAILED(res) && (aLen > 0));
          // we continue convert the bytes data into Unicode 
          // if we have conversion error and we have more data.

	  // delete[] unichars;
  }
  else {
    mBuffer.Append(aBuffer,aLen);
    mTotalRead+=aLen;
  }

  return PR_TRUE;
}


PRBool nsScanner::Append(const PRUnichar* aBuffer, PRUint32 aLen){
  mBuffer.Append(aBuffer,aLen);
  mTotalRead+=aLen;
  return PR_TRUE;
}

/** 
 * Grab data from underlying stream.
 *
 * @update  gess4/3/98
 * @return  error code
 */
nsresult nsScanner::FillBuffer(void) {
  nsresult result=NS_OK;

  if(!mInputStream) {
    //This is DEBUG code!!!!!!  XXX DEBUG XXX
    //If you're here, it means someone tried to load a
    //non-existent document. So as a favor, we emit a
    //little bit of HTML explaining the error.
    if(0==mTotalRead) {
      mBuffer.Append((const char*)kBadHTMLText);
      mBuffer.Append(mFilename);
    }
    else result=kEOF;
  }
  else {
    PRInt32 numread=0;
    char buf[kBufsize+1];
    buf[kBufsize]=0;

    if(mInputStream) {
    	numread = mInputStream->read(buf, kBufsize);
      if (0 == numread) {
        return kEOF;
      }
    }
    mOffset=mBuffer.Length();
    if((0<numread) && (0==result))
      mBuffer.Append((const char*)buf,numread);
    mTotalRead+=mBuffer.Length();
  }

  return result;
}

/**
 *  determine if the scanner has reached EOF
 *  
 *  @update  gess 5/12/98
 *  @param   
 *  @return  0=!eof 1=eof 
 */
nsresult nsScanner::Eof() {
  nsresult theError=NS_OK;

  if(mOffset>=(PRUint32)mBuffer.Length()) {
    theError=FillBuffer();  
  }
  
  if(NS_OK==theError) {
    if (0==(PRUint32)mBuffer.Length()) {
      return kEOF;
    }
  }

  return theError;
}

/**
 *  retrieve next char from scanners internal input stream
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  error code reflecting read status
 */
nsresult nsScanner::GetChar(PRUnichar& aChar) {
  nsresult result=NS_OK;
  aChar=0;  
  if(mOffset>=(PRUint32)mBuffer.Length()) 
    result=Eof();

  if(NS_OK == result) {
    aChar=GetCharAt(mBuffer,mOffset++);
  }
  return result;
}


/**
 *  peek ahead to consume next char from scanner's internal
 *  input buffer
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  
 */
nsresult nsScanner::Peek(PRUnichar& aChar) {
  nsresult result=NS_OK;
  aChar=0;  
  if(mOffset>=(PRUint32)mBuffer.Length()) 
    result=Eof();

  if(NS_OK == result) {
    aChar=GetCharAt(mBuffer,mOffset);
  }
  return result;
}


/**
 *  Push the given char back onto the scanner
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  error code
 */
nsresult nsScanner::PutBack(PRUnichar aChar) {
  if(mOffset>0)
    mOffset--;
  else mBuffer.Insert(aChar,0);
  return NS_OK;
}


/**
 *  Skip whitespace on scanner input stream
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  error status
 */
nsresult nsScanner::SkipWhitespace(void) {

  PRUnichar         theChar=0;
  nsresult          result=Peek(theChar);
  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;
  PRBool            found=PR_FALSE;  

#if 1
  while(NS_OK==result) {
 
    theChar=theBuf[mOffset++];
    if(theChar) {
      switch(theChar) {
        case ' ':
        case '\r':
        case '\n':
        case '\b':
        case '\t':
          found=PR_TRUE;
          break;
        default:
          found=PR_FALSE;
          break;
      }
      if(!found) {
        mOffset-=1;
        break;
      }
    }
    else {
      mOffset-=1;
      result=Peek(theChar);
      theBuf=mBuffer.GetUnicode();
      theOrigin=mOffset;
    }
  }

  //DoErrTest(aString);

  return result;

#endif

#if 0
  static const char* gSpaces=" \n\r\t\b";
  
  int len=strlen(gSpaces);
  CBufDescriptor buf(gSpaces,PR_TRUE,len+1,len);
  nsAutoString theWS(buf);
  return SkipOver(theWS);
#endif

}

/**
 *  Skip over chars as long as they equal given char
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  error code
 */
nsresult nsScanner::SkipOver(PRUnichar aSkipChar){

  PRUnichar ch=0;
  nsresult   result=NS_OK;

  while(NS_OK==result) {
    result=GetChar(ch);
    if(NS_OK == result) {
      if(ch!=aSkipChar) {
        PutBack(ch);
        break;
      }
    } 
    else break;
  } //while
  return result;

}

/**
 *  Skip over chars as long as they're in aSkipSet
 *  
 *  @update  gess 3/25/98
 *  @param   aSkipSet is an ordered string.
 *  @return  error code
 */
nsresult nsScanner::SkipOver(nsString& aSkipSet){

  PRUnichar theChar=0;
  nsresult  result=NS_OK;

  while(NS_OK==result) {
    result=GetChar(theChar);
    if(NS_OK == result) {
      PRInt32 pos=aSkipSet.FindChar(theChar);
      if(kNotFound==pos) {
        PutBack(theChar);
        break;
      }
    } 
    else break;
  } //while
  return result;

}


/**
 *  Skip over chars until they're in aValidSet
 *  
 *  @update  gess 3/25/98
 *  @param   aValid set is an ordered string that 
 *           contains chars you're looking for
 *  @return  error code
 */
nsresult nsScanner::SkipTo(nsString& aValidSet){
  PRUnichar ch=0;
  nsresult  result=NS_OK;

  while(NS_OK==result) {
    result=GetChar(ch);
    if(NS_OK == result) {
      PRInt32 pos=aValidSet.FindChar(ch);
      if(kNotFound!=pos) {
        PutBack(ch);
        break;
      }
    } 
    else break;
  } //while
  return result;
}

#if 0
void DoErrTest(nsString& aString) {
  PRInt32 pos=aString.FindChar(0);
  if(kNotFound<pos) {
    if(aString.Length()-1!=pos) {
    }
  }
}

void DoErrTest(nsCString& aString) {
  PRInt32 pos=aString.FindChar(0);
  if(kNotFound<pos) {
    if(aString.Length()-1!=pos) {
    }
  }
}
#endif

/**
 *  Skip over chars as long as they're in aValidSet
 *  
 *  @update  gess 3/25/98
 *  @param   aValidSet is an ordered string containing the 
 *           characters you want to skip
 *  @return  error code
 */
nsresult nsScanner::SkipPast(nsString& aValidSet){
  NS_NOTYETIMPLEMENTED("Error: SkipPast not yet implemented.");
  return NS_OK;
}

/**
 *  Consume characters until you find the terminal char
 *  
 *  @update  gess 3/25/98
 *  @param   aString receives new data from stream
 *  @param   addTerminal tells us whether to append terminal to aString
 *  @return  error code
 */
nsresult nsScanner::ReadIdentifier(nsString& aString) {

  PRUnichar         theChar=0;
  nsresult          result=Peek(theChar);
  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;
  PRBool            found=PR_FALSE;  

  while(NS_OK==result) {
 
    theChar=theBuf[mOffset++];
    if(theChar) {
      found=PR_FALSE;
      switch(theChar) {
        case ':':
        case '_':
        case '-':
          found=PR_TRUE;
          break;
        default:
          if(('a'<=theChar) && (theChar<='z'))
            found=PR_TRUE;
          else if(('A'<=theChar) && (theChar<='Z'))
            found=PR_TRUE;
          else if(('0'<=theChar) && (theChar<='9'))
            found=PR_TRUE;
          break;
      }

      if(!found) {
        mOffset-=1;
        aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
        break;
      }
    }
    else {
      aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
      mOffset-=1;
      result=Peek(theChar);
      theBuf=mBuffer.GetUnicode();
      theOrigin=mOffset;
    }
  }

  //DoErrTest(aString);

  return result;
}

/**
 *  Consume characters until you find the terminal char
 *  
 *  @update  gess 3/25/98
 *  @param   aString receives new data from stream
 *  @param   addTerminal tells us whether to append terminal to aString
 *  @return  error code
 */
nsresult nsScanner::ReadNumber(nsString& aString) {

  PRUnichar         theChar=0;
  nsresult          result=Peek(theChar);
  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;
  PRBool            found=PR_FALSE;  

  while(NS_OK==result) {
 
    theChar=theBuf[mOffset++];
    if(theChar) {
      found=PR_FALSE;
      if(('a'<=theChar) && (theChar<='f'))
        found=PR_TRUE;
      else if(('A'<=theChar) && (theChar<='F'))
        found=PR_TRUE;
      else if(('0'<=theChar) && (theChar<='9'))
        found=PR_TRUE;
      if(!found) {
        mOffset-=1;
        aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
        break;
      }
    }
    else {
      aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
      mOffset-=1;
      result=Peek(theChar);
      theBuf=mBuffer.GetUnicode();
      theOrigin=mOffset;
    }
  }

  //DoErrTest(aString);

  return result;
}

/**
 *  Consume characters until you find the terminal char
 *  
 *  @update  gess 3/25/98
 *  @param   aString receives new data from stream
 *  @param   addTerminal tells us whether to append terminal to aString
 *  @return  error code
 */
nsresult nsScanner::ReadWhitespace(nsString& aString) {

  PRUnichar         theChar=0;
  nsresult          result=Peek(theChar);
  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;
  PRBool            found=PR_FALSE;  

  while(NS_OK==result) {
 
    theChar=theBuf[mOffset++];
    if(theChar) {
      switch(theChar) {
        case ' ':
        case '\b':
        case '\t':
          found=PR_TRUE;
          break;
        default:
          found=PR_FALSE;
          break;
      }
      if(!found) {
        mOffset-=1;
        aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
        break;
      }
    }
    else {
      aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
      mOffset-=1;
      result=Peek(theChar);
      theBuf=mBuffer.GetUnicode();
      theOrigin=mOffset;
    }
  }

  //DoErrTest(aString);

  return result;
}

/**
 *  Consume chars as long as they are <i>in</i> the 
 *  given validSet of input chars.
 *  
 *  @update  gess 3/25/98
 *  @param   aString will contain the result of this method
 *  @param   aValidSet is an ordered string that contains the
 *           valid characters
 *  @return  error code
 */
nsresult nsScanner::ReadWhile(nsString& aString,
                             nsString& aValidSet,
                             PRBool anOrderedSet,
                             PRBool addTerminal){

  NS_ASSERTION(((PR_FALSE==anOrderedSet) || aValidSet.IsOrdered()),kUnorderedStringError);

  PRUnichar         theChar=0;
  nsresult          result=Peek(theChar);
  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;

  while(NS_OK==result) {
 
    theChar=theBuf[mOffset++];
    if(theChar) {
      PRInt32 pos=(anOrderedSet) ? aValidSet.BinarySearch(theChar) : aValidSet.FindChar(theChar);
      if(kNotFound==pos) {
        if(!addTerminal)
          mOffset-=1;
        aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
        break;
      }
    }
    else {
      aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
      mOffset-=1;
      result=Peek(theChar);
      theBuf=mBuffer.GetUnicode();
      theOrigin=mOffset;
    }
  }

  //DoErrTest(aString);

  return result;

}

/**
 *  Consume chars as long as they are <i>in</i> the 
 *  given validSet of input chars.
 *  
 *  @update  gess 3/25/98
 *  @param   aString will contain the result of this method
 *  @param   aValidSet is an ordered string that contains the
 *           valid characters
 *  @return  error code
 */
nsresult nsScanner::ReadWhile(nsString& aString,
                             nsCString& aValidSet,
                             PRBool anOrderedSet,
                             PRBool addTerminal){

  NS_ASSERTION(((PR_FALSE==anOrderedSet) || aValidSet.IsOrdered()),kUnorderedStringError);

  PRUnichar         theChar=0;
  nsresult          result=Peek(theChar);
  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;

  while(NS_OK==result) {
 
    theChar=theBuf[mOffset++];
    if(theChar) {
      PRInt32 pos=(anOrderedSet) ? aValidSet.BinarySearch(theChar) : aValidSet.FindChar(theChar);
      if(kNotFound==pos) {
        if(!addTerminal)
          mOffset-=1;
        aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
        break;
      }
    }
    else {
      aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
      mOffset-=1;
      result=Peek(theChar);
      theBuf=mBuffer.GetUnicode();
      theOrigin=mOffset;
    }
  }

  //DoErrTest(aString);

  return result;

}

/**
 *  Consume chars as long as they are <i>in</i> the 
 *  given validSet of input chars.
 *  
 *  @update  gess 3/25/98
 *  @param   aString will contain the result of this method
 *  @param   anInputSet contains the legal input chars
 *           valid characters
 *  @return  error code
 */
nsresult nsScanner::ReadWhile(nsString& aString,
                             const char* anInputSet,
                             PRBool anOrderedSet,
                             PRBool addTerminal)
{

  nsresult   result=NS_OK;
  if(anInputSet) {
    PRInt32 len=nsCRT::strlen(anInputSet);
    if(0<len) {

      CBufDescriptor buf(anInputSet,PR_TRUE,len+1,len);
      nsCAutoString theSet(buf);

      result=ReadWhile(aString,theSet,anOrderedSet,addTerminal);
    } //if
  }//if
  return result;
}

/**
 *  Consume characters until you encounter one contained in given
 *  input set.
 *  
 *  @update  gess 3/25/98
 *  @param   aString will contain the result of this method
 *  @param   aTerminalSet is an ordered string that contains
 *           the set of INVALID characters
 *  @return  error code
 */
nsresult nsScanner::ReadUntil(nsString& aString,
                             nsString& aTerminalSet,
                             PRBool anOrderedSet,
                             PRBool addTerminal){
  
  NS_ASSERTION(((PR_FALSE==anOrderedSet) || aTerminalSet.IsOrdered()),kUnorderedStringError);


  PRUnichar         theChar=0;
  nsresult          result=Peek(theChar);
  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;

  while(NS_OK==result) {
 
    theChar=theBuf[mOffset++];
    if(theChar) {
      PRInt32 pos=(anOrderedSet) ? aTerminalSet.BinarySearch(theChar) : aTerminalSet.FindChar(theChar);
      if(kNotFound!=pos) {
        if(!addTerminal)
          mOffset-=1;
        aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
        break;
      }
    }
    else {
      aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
      mOffset-=1;
      result=Peek(theChar);
      theBuf=mBuffer.GetUnicode();
      theOrigin=mOffset;
    }
  }

  //DoErrTest(aString);

  return result;

}

/**
 *  Consume characters until you encounter one contained in given
 *  input set.
 *  
 *  @update  gess 3/25/98
 *  @param   aString will contain the result of this method
 *  @param   aTerminalSet is an ordered string that contains
 *           the set of INVALID characters
 *  @return  error code
 */
nsresult nsScanner::ReadUntil(nsString& aString,
                             nsCString& aTerminalSet,
                             PRBool anOrderedSet,
                             PRBool addTerminal){
  
  NS_ASSERTION(((PR_FALSE==anOrderedSet) || aTerminalSet.IsOrdered()),kUnorderedStringError);


  PRUnichar         theChar=0;
  nsresult          result=Peek(theChar);
  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;

  while(NS_OK==result) {
 
    theChar=theBuf[mOffset++];
    if(theChar) {
      PRInt32 pos=(anOrderedSet) ? aTerminalSet.BinarySearch(theChar) : aTerminalSet.FindChar(theChar);
      if(kNotFound!=pos) {
        if(!addTerminal)
          mOffset-=1;
        aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
        break;
      }
    }
    else {
      aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
      mOffset-=1;
      result=Peek(theChar);
      theBuf=mBuffer.GetUnicode();
      theOrigin=mOffset;
    }
  }

  //DoErrTest(aString);

  return result;
}


/**
 *  Consume characters until you encounter one contained in given
 *  input set.
 *  
 *  @update  gess 3/25/98
 *  @param   aString will contain the result of this method
 *  @param   aTerminalSet is an ordered string that contains
 *           the set of INVALID characters
 *  @return  error code
 */
nsresult nsScanner::ReadUntil(nsString& aString,
                              const char* aTerminalSet,
                             PRBool anOrderedSet,
                             PRBool addTerminal)
{
  nsresult   result=NS_OK;
  if(aTerminalSet) {
    PRInt32 len=nsCRT::strlen(aTerminalSet);
    if(0<len) {

      CBufDescriptor buf(aTerminalSet,PR_TRUE,len+1,len);
      nsCAutoString theSet(buf);

      result=ReadUntil(aString,theSet,anOrderedSet,addTerminal);
    } //if
  }//if
  return result;
}

/**
 *  Consumes chars until you see the given terminalChar
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  error code
 */
nsresult nsScanner::ReadUntil(nsString& aString,
                             PRUnichar aTerminalChar,
                             PRBool addTerminal){
  PRUnichar theChar=0;
  nsresult  result=NS_OK;

  const PRUnichar*  theBuf=mBuffer.GetUnicode();
  PRInt32           theOrigin=mOffset;
  result=Peek(theChar);
  PRUint32 theLen=mBuffer.Length();

  while(NS_OK==result) {
    
    theChar=theBuf[mOffset++];
    if(theChar) {
      if(aTerminalChar==theChar) {
        if(!addTerminal)
          mOffset-=1;
        aString.Append(&theBuf[theOrigin],mOffset-theOrigin);
        break;
      }
    }
    else {
      aString.Append(&theBuf[theOrigin],theLen-theOrigin-1);
      mOffset=theLen;
      result=Peek(theChar);
      theLen=mBuffer.Length();
    }
  }

  //DoErrTest(aString);
  return result;

}

/**
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  
 */
nsString& nsScanner::GetBuffer(void) {
  return mBuffer;
}

/**
 *  Call this to copy bytes out of the scanner that have not yet been consumed
 *  by the tokenization process.
 *  
 *  @update  gess 5/12/98
 *  @param   aCopyBuffer is where the scanner buffer will be copied to
 *  @return  nada
 */
void nsScanner::CopyUnusedData(nsString& aCopyBuffer) {
  PRInt32 theLen=mBuffer.Length();
  if(0<theLen) {
    mBuffer.Right(aCopyBuffer,theLen-mMarkPos);
  }
}

/**
 *  Retrieve the name of the file that the scanner is reading from.
 *  In some cases, it's just a given name, because the scanner isn't
 *  really reading from a file.
 *  
 *  @update  gess 5/12/98
 *  @return  
 */
nsString& nsScanner::GetFilename(void) {
  return mFilename;
}

/**
 *  Conduct self test. Actually, selftesting for this class
 *  occurs in the parser selftest.
 *  
 *  @update  gess 3/25/98
 *  @param   
 *  @return  
 */

void nsScanner::SelfTest(void) {
#ifdef _DEBUG
#endif
}



