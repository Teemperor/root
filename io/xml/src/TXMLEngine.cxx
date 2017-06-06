// @(#)root/xml:$Id: 1bd040ac1c03c58bcad15b5206d602680a831c0a $
// Author: Sergey Linev  10.05.2004

/*************************************************************************
 * Copyright (C) 1995-2004, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//________________________________________________________________________
//
//  TXMLEngine class is used to write and read ROOT XML files - TXMLFile.
//  It does not conform to complete xml standard and cannot be used
//  as parser for arbitrary XML files. For such cases TXMLParser should
//  be used. This class was introduced to exclude dependency from
//  external libraries (like libxml2) and improve speed / memory consumption.
//
//________________________________________________________________________

#include "TXMLEngine.h"

#include "Riostream.h"
#include "TString.h"
#include "TNamed.h"
#include "TObjArray.h"
#include <stdlib.h>
#include <string.h>

ClassImp(TXMLEngine);

struct SXmlAttr_t {
   SXmlAttr_t  *fNext;
   // after structure itself memory for attribute name is preserved
   // if first byte is 0, this is special attribute
   static inline char* Name(void* arg) { return (char*)arg + sizeof(SXmlAttr_t); }
};

enum EXmlNodeType {
  kXML_NODE    = 1,    // normal node with children
  kXML_COMMENT = 2,    // comment (stored as value of node fName)
  kXML_PI_NODE = 3,    // processing instructions node (like <?name  attr="" ?>
  kXML_RAWLINE = 4,    // just one line of xml code
  kXML_CONTENT = 5     // node content, can appear many times in between of normal nodes
};

struct SXmlNode_t {
   EXmlNodeType fType;    //  this is node type - node, comment, processing instruction and so on
   SXmlAttr_t  *fAttr;    // first attribute
   SXmlAttr_t  *fNs;      // name space definition (if any)
   SXmlNode_t  *fNext;    // next node on the same level of hierarchy
   SXmlNode_t  *fChild;   // first child node
   SXmlNode_t  *fLastChild; // last child node
   SXmlNode_t  *fParent;   // parent node
   // consequent bytes after structure are node name
   // if first byte is 0, next is node content
   static inline char* Name(void* arg) { return (char*)arg + sizeof(SXmlNode_t); }
};

struct SXmlDoc_t {
   SXmlNode_t  *fRootNode;
   char        *fDtdName;
   char        *fDtdRoot;
};

class TXMLOutputStream {
protected:

   std::ostream  *fOut;
   TString       *fOutStr;
   char          *fBuf;
   char          *fCurrent;
   char          *fMaxAddr;
   char          *fLimitAddr;

public:
   TXMLOutputStream(const char* filename, Int_t bufsize = 20000)
   {
      fOut = new std::ofstream(filename);
      fOutStr = nullptr;
      Init(bufsize);
   }

   TXMLOutputStream(TString* outstr, Int_t bufsize = 20000)
   {
      fOut = nullptr;
      fOutStr = outstr;
      Init(bufsize);
   }

   void Init(Int_t bufsize)
   {
      fBuf = (char*) malloc(bufsize);
      fCurrent = fBuf;
      fMaxAddr = fBuf + bufsize;
      fLimitAddr = fBuf + int(bufsize*0.75);
   }

   virtual ~TXMLOutputStream()
   {
      if (fCurrent!=fBuf) OutputCurrent();
      delete fOut;
      free(fBuf);
   }

   void OutputCurrent()
   {
      if (fCurrent!=fBuf) {
         if (fOut != nullptr)
            fOut->write(fBuf, fCurrent-fBuf);
         else if (fOutStr != nullptr)
            fOutStr->Append(fBuf, fCurrent-fBuf);
      }
      fCurrent = fBuf;
   }

   void OutputChar(char symb)
   {
      if (fOut != nullptr)
         fOut->put(symb);
      else if (fOutStr != nullptr)
         fOutStr->Append(symb);
   }

   void Write(const char* str)
   {
      int len = strlen(str);
      if (fCurrent+len>=fMaxAddr) {
         OutputCurrent();
         fOut->write(str,len);
      } else {
         while (*str)
           *fCurrent++ = *str++;
         if (fCurrent>fLimitAddr)
            OutputCurrent();
      }
   }

   void Put(char symb, Int_t cnt=1)
   {
      if (fCurrent+cnt>=fMaxAddr)
         OutputCurrent();
      if (fCurrent+cnt>=fMaxAddr)
         for(int n=0;n<cnt;n++)
            OutputChar(symb);
      else {
         for(int n=0;n<cnt;n++)
            *fCurrent++ = symb;
         if (fCurrent>fLimitAddr)
            OutputCurrent();
      }
   }
};

class TXMLEntity : public TNamed {
   Bool_t      fSystem;   //!  is system (file)
public:
   TXMLEntity() : TNamed(), fSystem(kFALSE) {}
   TXMLEntity(const TString& name, const TString& value, Bool_t sys) : TNamed(name, value), fSystem(sys) {}
   Bool_t IsSystem() const { return fSystem; }
};


class TXMLInputStream {
protected:

   std::istream  *fInp;
   const char    *fInpStr;
   Int_t          fInpStrLen;

   char          *fBuf;
   Int_t          fBufSize;

   char          *fMaxAddr;
   char          *fLimitAddr;

   Int_t          fTotalPos;
   Int_t          fCurrentLine;

   TObjArray      fEntities;   //! array of TXMLEntity

public:

   char           *fCurrent;

   TXMLInputStream(Bool_t isfilename, const char *filename, Int_t ibufsize)
      : fInp(nullptr), fInpStr(nullptr), fInpStrLen(0), fBuf(nullptr), fBufSize(0), fMaxAddr(nullptr),
        fLimitAddr(nullptr), fTotalPos(0), fCurrentLine(0), fEntities(), fCurrent(nullptr)
   {
      if (isfilename) {
         fInp = new std::ifstream(filename);
         fInpStr = nullptr;
         fInpStrLen = 0;
      } else {
         fInp = nullptr;
         fInpStr = filename;
         fInpStrLen = filename == nullptr ? 0 : strlen(filename);
      }

      fBufSize = ibufsize;
      fBuf = (char*) malloc(fBufSize);

      fCurrent = nullptr;
      fMaxAddr = nullptr;

      int len = DoRead(fBuf, fBufSize);
      fCurrent = fBuf;
      fMaxAddr = fBuf+len;
      fLimitAddr = fBuf + int(len*0.75);

      fTotalPos = 0;
      fCurrentLine = 1;

      fEntities.SetOwner(kTRUE);
   }

   virtual ~TXMLInputStream()
   {
      delete fInp;
      fInp = nullptr;
      free(fBuf);
      fBuf = nullptr;
   }

   inline Bool_t EndOfFile() { return (fInp != nullptr) ? fInp->eof() : (fInpStrLen <= 0); }

   inline Bool_t EndOfStream() { return EndOfFile() && (fCurrent>=fMaxAddr); }

   void AddEntity(TXMLEntity* ent) { fEntities.Add(ent); }

   Int_t NumEntities() const { return fEntities.GetLast() + 1; }

   TXMLEntity* FindEntity(const char* beg, Int_t len)
   {
      if (len <= 0) return nullptr;
      for (Int_t n=0; n<=fEntities.GetLast(); n++) {
         TXMLEntity* entity = (TXMLEntity*) fEntities[n];
         if ((Int_t) strlen(entity->GetName()) != len) continue;
         if (strncmp(beg, entity->GetName(), len)==0) return entity;
      }
      return nullptr;
   }


   int DoRead(char* buf, int maxsize)
   {
      if (EndOfFile()) return 0;
      if (fInp != nullptr) {
         fInp->get(buf, maxsize, 0);
         maxsize = strlen(buf);
      } else {
         if (maxsize>fInpStrLen) maxsize = fInpStrLen;
         strncpy(buf, fInpStr, maxsize);
         fInpStr+=maxsize;
         fInpStrLen-=maxsize;
      }
      return maxsize;
   }

   Bool_t ExpandStream()
   {
      if (EndOfFile()) return kFALSE;
      fBufSize*=2;
      int curlength = fMaxAddr - fBuf;
      char* newbuf = (char*) realloc(fBuf, fBufSize);
      if (newbuf == nullptr) return kFALSE;

      fMaxAddr = newbuf + (fMaxAddr - fBuf);
      fCurrent = newbuf + (fCurrent - fBuf);
      fLimitAddr = newbuf + (fLimitAddr - fBuf);
      fBuf = newbuf;

      int len = DoRead(fMaxAddr, fBufSize-curlength);
      if (len==0) return kFALSE;
      fMaxAddr += len;
      fLimitAddr += int(len*0.75);
      return kTRUE;
   }

   Bool_t ShiftStream()
   {
      if (fCurrent<fLimitAddr) return kTRUE; // everything ok, can continue
      if (EndOfFile()) return kTRUE;
      int rest_len = fMaxAddr - fCurrent;
      memmove(fBuf, fCurrent, rest_len);
      int read_len = DoRead(fBuf + rest_len, fBufSize - rest_len);

      fCurrent = fBuf;
      fMaxAddr = fBuf + rest_len + read_len;
      fLimitAddr = fBuf + int((rest_len + read_len)*0.75);
      return kTRUE;
   }

   Int_t  TotalPos() { return fTotalPos; }

   Int_t CurrentLine() { return fCurrentLine; }

   Bool_t ShiftCurrent(Int_t sz = 1)
   {
      for(int n=0;n<sz;n++) {
         if (*fCurrent==10) fCurrentLine++;
         if (fCurrent>=fLimitAddr) {
            ShiftStream();
            if (fCurrent>=fMaxAddr) return kFALSE;
         }
         fCurrent++;
      }
      fTotalPos+=sz;
      return kTRUE;
   }

   Bool_t SkipSpaces(Bool_t tillendl = kFALSE)
   {
      while (fCurrent<fMaxAddr) {
         char symb = *fCurrent;
         if ((symb>26) && (symb!=' ')) return kTRUE;

         if (!ShiftCurrent()) return kFALSE;

         if (tillendl && (symb==10)) return kTRUE;
      }
      return kFALSE;
   }

   Bool_t CheckFor(const char* str)
   {
      // Check if in current position we see specified string
      int len = strlen(str);
      while (fCurrent+len>fMaxAddr)
         if (!ExpandStream()) return kFALSE;
      char* curr = fCurrent;
      while (*str != 0)
         if (*str++ != *curr++) return kFALSE;
      return ShiftCurrent(len);
   }

   Int_t SearchFor(const char* str)
   {
      // Serach for specified string in the stream
      // return number of symbols before string was found, -1 if error
      int len = strlen(str);

      char* curr = fCurrent;

      do {
         curr++;
         while (curr+len>fMaxAddr)
            if (!ExpandStream()) return -1;
         char* chk0 = curr;
         const char* chk = str;
         Bool_t find = kTRUE;
         while (*chk != 0)
            if (*chk++ != *chk0++) { find = kFALSE; break; }
         // if string found, shift to the next symbol after string
         if (find) return curr - fCurrent;
      } while (curr<fMaxAddr);
      return -1;
   }

#define GoodStartSymbol(symb) \
   (((symb>='a') && (symb<='z')) || ((symb>='A') && (symb<='Z')) || (symb=='_') || \
    ((symb>=0xc0) && (symb<=0xd6)) || ((symb>=0xd8) && (symb<=0xf6)) || (symb>0xf8))

   Int_t LocateIdentifier()
   {
      unsigned char symb = (unsigned char) *fCurrent;

      Bool_t ok = GoodStartSymbol(symb);
      if (!ok) return 0;

      char* curr = fCurrent;

      do {
         curr++;
         if (curr>=fMaxAddr)
            if (!ExpandStream()) return 0;
         symb = (unsigned char) *curr;
         ok = GoodStartSymbol(symb) ||
              ((symb>='0') && (symb<='9')) || (symb==':')  || (symb=='-') || (symb=='.') || (symb==0xb7);
         if (!ok) return curr-fCurrent;
      } while (curr<fMaxAddr);
      return 0;
   }

   Int_t LocateContent()
   {
      char* curr = fCurrent;
      while (curr<fMaxAddr) {
         char symb = *curr;
         if (symb=='<') return curr - fCurrent;
         curr++;
         if (curr>=fMaxAddr)
            if (!ExpandStream()) return -1;
      }
      return -1;
   }

   Int_t LocateValue(char* start, bool withequalsign = true)
   {
      char* curr = start;
      if (curr>=fMaxAddr)
         if (!ExpandStream()) return 0;
      if (withequalsign) {
         if (*curr!='=') return 0;
         curr++;
         if (curr>=fMaxAddr)
            if (!ExpandStream()) return 0;
      }
      if ((*curr!='\"') && (*curr!='\'')) return 0;
      char quote = *curr;
      do {
         curr++;
         if (curr>=fMaxAddr)
            if (!ExpandStream()) return 0;
         if (*curr==quote) return curr-start+1;
      } while (curr<fMaxAddr);
      return 0;
   }
};

////////////////////////////////////////////////////////////////////////////////
/// default (normal) constructor of TXMLEngine class

TXMLEngine::TXMLEngine()
{
   fSkipComments = kFALSE;
}


////////////////////////////////////////////////////////////////////////////////
/// destructor for TXMLEngine object

TXMLEngine::~TXMLEngine()
{
}

////////////////////////////////////////////////////////////////////////////////
/// checks if node has attribute of specified name

Bool_t TXMLEngine::HasAttr(XMLNodePointer_t xmlnode, const char* name)
{
   if ((xmlnode == nullptr) || (name == nullptr)) return kFALSE;
   SXmlAttr_t* attr = ((SXmlNode_t*)xmlnode)->fAttr;
   while (attr != nullptr) {
      if (strcmp(SXmlAttr_t::Name(attr),name)==0) return kTRUE;
      attr = attr->fNext;
   }
   return kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// returns value of attribute for xmlnode

const char* TXMLEngine::GetAttr(XMLNodePointer_t xmlnode, const char* name)
{
   if (xmlnode == nullptr) return nullptr;
   SXmlAttr_t* attr = ((SXmlNode_t*)xmlnode)->fAttr;
   while (attr != nullptr) {
      if (strcmp(SXmlAttr_t::Name(attr),name)==0)
         return SXmlAttr_t::Name(attr) + strlen(name) + 1;
      attr = attr->fNext;
   }
   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// returns value of attribute as integer

Int_t TXMLEngine::GetIntAttr(XMLNodePointer_t xmlnode, const char* name)
{
   if (xmlnode == nullptr) return 0;
   Int_t res = 0;
   const char* attr = GetAttr(xmlnode, name);
   if (attr) sscanf(attr, "%d", &res);
   return res;
}

////////////////////////////////////////////////////////////////////////////////
/// creates new attribute for xmlnode,
/// namespaces are not supported for attributes

XMLAttrPointer_t TXMLEngine::NewAttr(XMLNodePointer_t xmlnode, XMLNsPointer_t,
                                     const char* name, const char* value)
{
   if (xmlnode == nullptr) return nullptr;

   int namelen(name != nullptr ? strlen(name) : 0);
   int valuelen(value != nullptr ? strlen(value) : 0);
   SXmlAttr_t* attr = (SXmlAttr_t*) AllocateAttr(namelen, valuelen, xmlnode);

   char* attrname = SXmlAttr_t::Name(attr);
   if (namelen>0)
      strncpy(attrname, name, namelen+1);
   else
      *attrname = 0;
   attrname += (namelen + 1);
   if (valuelen>0)
      strncpy(attrname, value, valuelen+1);
   else
      *attrname = 0;

   return (XMLAttrPointer_t) attr;
}

////////////////////////////////////////////////////////////////////////////////
/// create node attribute with integer value

XMLAttrPointer_t TXMLEngine::NewIntAttr(XMLNodePointer_t xmlnode,
                                      const char* name,
                                      Int_t value)
{
   char sbuf[30];
   sprintf(sbuf,"%d",value);
   return NewAttr(xmlnode, nullptr, name, sbuf);
}

////////////////////////////////////////////////////////////////////////////////
/// remove attribute from xmlnode

void TXMLEngine::FreeAttr(XMLNodePointer_t xmlnode, const char* name)
{
   if (xmlnode == nullptr) return;
   SXmlAttr_t* attr = ((SXmlNode_t*) xmlnode)->fAttr;
   SXmlAttr_t *prev = nullptr;
   while (attr != nullptr) {
      if (strcmp(SXmlAttr_t::Name(attr),name)==0) {
         if (prev != nullptr)
            prev->fNext = attr->fNext;
         else
            ((SXmlNode_t*) xmlnode)->fAttr = attr->fNext;
         //fNumNodes--;
         free(attr);
         return;
      }

      prev = attr;
      attr = attr->fNext;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Free all attributes of the node

void TXMLEngine::FreeAllAttr(XMLNodePointer_t xmlnode)
{
   if (xmlnode == nullptr) return;

   SXmlNode_t* node = (SXmlNode_t*) xmlnode;
   SXmlAttr_t* attr = node->fAttr;
   while (attr != nullptr) {
      SXmlAttr_t* next = attr->fNext;
      free(attr);
      attr = next;
   }
   node->fAttr = nullptr;
}


////////////////////////////////////////////////////////////////////////////////
/// return first attribute in the list, namespace (if exists) will be skiped

XMLAttrPointer_t TXMLEngine::GetFirstAttr(XMLNodePointer_t xmlnode)
{
   if (xmlnode == nullptr) return nullptr;
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;

   SXmlAttr_t* attr = node->fAttr;
   if ((attr != nullptr) && (node->fNs == attr)) attr = attr->fNext;

   return (XMLAttrPointer_t) attr;
}

////////////////////////////////////////////////////////////////////////////////
/// return next attribute in the list

XMLAttrPointer_t TXMLEngine::GetNextAttr(XMLAttrPointer_t xmlattr)
{
   if (xmlattr == nullptr) return nullptr;

   return (XMLAttrPointer_t) ((SXmlAttr_t*) xmlattr)->fNext;
}

////////////////////////////////////////////////////////////////////////////////
/// return name of the attribute

const char* TXMLEngine::GetAttrName(XMLAttrPointer_t xmlattr)
{
   if (xmlattr == nullptr) return nullptr;

   return SXmlAttr_t::Name(xmlattr);

}

////////////////////////////////////////////////////////////////////////////////
/// return value of attribute

const char* TXMLEngine::GetAttrValue(XMLAttrPointer_t xmlattr)
{
   if (xmlattr == nullptr) return nullptr;

   const char* attrname = SXmlAttr_t::Name(xmlattr);
   return attrname + strlen(attrname) + 1;
}

////////////////////////////////////////////////////////////////////////////////
/// create new child element for parent node

XMLNodePointer_t TXMLEngine::NewChild(XMLNodePointer_t parent, XMLNsPointer_t ns,
                                      const char* name, const char* content)
{
   int namelen(name != nullptr ? strlen(name) : 0);

   SXmlNode_t* node = (SXmlNode_t*) AllocateNode(namelen, parent);

   if (namelen>0)
      strncpy(SXmlNode_t::Name(node), name, namelen+1);
   else
      *SXmlNode_t::Name(node) = 0;

   node->fNs = (SXmlAttr_t*) ns;
   int contlen = (content != nullptr) ? strlen(content) : 0;
   if (contlen>0) {
      SXmlNode_t* contnode = (SXmlNode_t*) AllocateNode(contlen, node);
      contnode->fType = kXML_CONTENT; // indicate that we creating content node
      strncpy(SXmlNode_t::Name(contnode), content, contlen+1);
   }

   return (XMLNodePointer_t) node;
}

////////////////////////////////////////////////////////////////////////////////
/// create namespace attribute for xmlnode.
/// namespace attribute will be always the first in list of node attributes

XMLNsPointer_t TXMLEngine::NewNS(XMLNodePointer_t xmlnode, const char* reference, const char* name)
{
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;
   if (name == nullptr) name = SXmlNode_t::Name(node);
   int namelen = strlen(name);
   char* nsname = new char[namelen+7];
   snprintf(nsname, namelen+7, "xmlns:%s", name);

   SXmlAttr_t* first = node->fAttr;
   node->fAttr = nullptr;

   SXmlAttr_t *nsattr = (SXmlAttr_t *)NewAttr(xmlnode, nullptr, nsname, reference);

   node->fAttr = nsattr;
   nsattr->fNext = first;

   node->fNs = nsattr;
   delete[] nsname;
   return (XMLNsPointer_t) nsattr;
}

////////////////////////////////////////////////////////////////////////////////
/// return namespace attribute  (if exists)

XMLNsPointer_t TXMLEngine::GetNS(XMLNodePointer_t xmlnode)
{
   if (xmlnode == nullptr) return nullptr;
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;

   return (XMLNsPointer_t) node->fNs;
}

////////////////////////////////////////////////////////////////////////////////
/// return name id of namespace

const char* TXMLEngine::GetNSName(XMLNsPointer_t ns)
{
   const char* nsname = GetAttrName((XMLAttrPointer_t)ns);

   if ((nsname != nullptr) && (strncmp(nsname, "xmlns:", 6) == 0)) nsname += 6;

   return nsname;
}

////////////////////////////////////////////////////////////////////////////////
/// return reference id of namespace

const char* TXMLEngine::GetNSReference(XMLNsPointer_t ns)
{
   return GetAttrValue((XMLAttrPointer_t)ns);
}


////////////////////////////////////////////////////////////////////////////////
/// add child element to xmlnode

void TXMLEngine::AddChild(XMLNodePointer_t parent, XMLNodePointer_t child)
{
   if ((parent == nullptr) || (child == nullptr)) return;
   SXmlNode_t* pnode = (SXmlNode_t*) parent;
   SXmlNode_t* cnode = (SXmlNode_t*) child;
   cnode->fParent = pnode;
   if (pnode->fLastChild == nullptr) {
      pnode->fChild = cnode;
      pnode->fLastChild = cnode;
   } else {
      //SXmlNode_t* ch = pnode->fChild;
      //while (ch->fNext!=0) ch=ch->fNext;
      pnode->fLastChild->fNext = cnode;
      pnode->fLastChild = cnode;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// add node as first child

void TXMLEngine::AddChildFirst(XMLNodePointer_t parent, XMLNodePointer_t child)
{
   if ((parent == nullptr) || (child == nullptr)) return;
   SXmlNode_t* pnode = (SXmlNode_t*) parent;
   SXmlNode_t* cnode = (SXmlNode_t*) child;
   cnode->fParent = pnode;

   cnode->fNext = pnode->fChild;
   pnode->fChild = cnode;

   if (pnode->fLastChild == nullptr) pnode->fLastChild = cnode;
}


////////////////////////////////////////////////////////////////////////////////
/// Adds comment line to the node

Bool_t TXMLEngine::AddComment(XMLNodePointer_t xmlnode, const char* comment)
{
   if ((xmlnode == nullptr) || (comment == nullptr)) return kFALSE;

   int commentlen = strlen(comment);

   SXmlNode_t* node = (SXmlNode_t*) AllocateNode(commentlen, xmlnode);
   node->fType = kXML_COMMENT;
   strncpy(SXmlNode_t::Name(node), comment, commentlen+1);

   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// add comment line to the top of the document

Bool_t TXMLEngine::AddDocComment(XMLDocPointer_t xmldoc, const char* comment)
{
   if (xmldoc == nullptr) return kFALSE;

   XMLNodePointer_t rootnode = DocGetRootElement(xmldoc);
   UnlinkNode(rootnode);

   Bool_t res = AddComment(((SXmlDoc_t*)xmldoc)->fRootNode, comment);

   AddChild((XMLNodePointer_t) ((SXmlDoc_t*)xmldoc)->fRootNode, rootnode);

   return res;
}

////////////////////////////////////////////////////////////////////////////////
/// Add just line into xml file
/// Line should has correct xml syntax that later it can be decoded by xml parser
/// For instance, it can be comment or processing instructions

Bool_t TXMLEngine::AddRawLine(XMLNodePointer_t xmlnode, const char* line)
{
   if ((xmlnode == nullptr) || (line == nullptr)) return kFALSE;

   int linelen = strlen(line);
   SXmlNode_t* node = (SXmlNode_t*) AllocateNode(linelen, xmlnode);
   node->fType = kXML_RAWLINE;
   strncpy(SXmlNode_t::Name(node), line, linelen+1);

   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Add just line on the top of xml document
/// Line should has correct xml syntax that later it can be decoded by xml parser

Bool_t TXMLEngine::AddDocRawLine(XMLDocPointer_t xmldoc, const char* line)
{
   XMLNodePointer_t rootnode = DocGetRootElement(xmldoc);
   UnlinkNode(rootnode);

   Bool_t res = AddRawLine(((SXmlDoc_t*)xmldoc)->fRootNode, line);

   AddChild((XMLNodePointer_t) ((SXmlDoc_t*)xmldoc)->fRootNode, rootnode);

   return res;

}


////////////////////////////////////////////////////////////////////////////////
/// Adds style sheet definition to the specified node
/// Creates <?xml-stylesheet alternate="yes" title="compact" href="small-base.css" type="text/css"?>
/// Attributes href and type must be supplied,
///  other attributes: title, alternate, media, charset are optional
/// if alternate==0, attribyte alternate="no" will be created,
/// if alternate>0, attribute alternate="yes"
/// if alternate<0, attribute will not be created

Bool_t TXMLEngine::AddStyleSheet(XMLNodePointer_t xmlnode,
                                 const char* href,
                                 const char* type,
                                 const char* title,
                                 int alternate,
                                 const char* media,
                                 const char* charset)
{
   if ((xmlnode == nullptr) || (href == nullptr) || (type == nullptr)) return kFALSE;

   const char* nodename = "xml-stylesheet";
   int nodenamelen = strlen(nodename);

   SXmlNode_t* node = (SXmlNode_t*) AllocateNode(nodenamelen, xmlnode);
   node->fType = kXML_PI_NODE;
   strncpy(SXmlNode_t::Name(node), nodename, nodenamelen+1);

   if (alternate >= 0) NewAttr(node, nullptr, "alternate", (alternate > 0) ? "yes" : "no");

   if (title != nullptr) NewAttr(node, nullptr, "title", title);

   NewAttr(node, nullptr, "href", href);
   NewAttr(node, nullptr, "type", type);

   if (media != nullptr) NewAttr(node, nullptr, "media", media);
   if (charset != nullptr) NewAttr(node, nullptr, "charset", charset);

   return kTRUE;
}


////////////////////////////////////////////////////////////////////////////////
/// Add style sheet definition on the top of document

Bool_t TXMLEngine::AddDocStyleSheet(XMLDocPointer_t xmldoc,
                                    const char* href,
                                    const char* type,
                                    const char* title,
                                    int alternate,
                                    const char* media,
                                    const char* charset)
{
   if (xmldoc == nullptr) return kFALSE;

   XMLNodePointer_t rootnode = DocGetRootElement(xmldoc);
   UnlinkNode(rootnode);

   Bool_t res = AddStyleSheet(((SXmlDoc_t*)xmldoc)->fRootNode,
                            href,type,title,alternate,media,charset);

   AddChild((XMLNodePointer_t) ((SXmlDoc_t*)xmldoc)->fRootNode, rootnode);

   return res;
}

////////////////////////////////////////////////////////////////////////////////
/// unlink (dettach) xml node from parent

void TXMLEngine::UnlinkNode(XMLNodePointer_t xmlnode)
{
   if (xmlnode == nullptr) return;
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;

   SXmlNode_t* parent = node->fParent;

   if (parent == nullptr) return;

   if (parent->fChild==node) {
      parent->fChild = node->fNext;
      if (parent->fLastChild==node)
         parent->fLastChild = node->fNext;
   } else {
      SXmlNode_t* ch = parent->fChild;
      while (ch->fNext!=node) ch = ch->fNext;
      ch->fNext = node->fNext;
      if (parent->fLastChild == node)
         parent->fLastChild = ch;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// release all memory, allocated fro this node and
/// destroyes node itself

void TXMLEngine::FreeNode(XMLNodePointer_t xmlnode)
{
   if (xmlnode == nullptr) return;
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;

   SXmlNode_t* child = node->fChild;
   while (child != nullptr) {
      SXmlNode_t* next = child->fNext;
      FreeNode((XMLNodePointer_t)child);
      child = next;
   }

   SXmlAttr_t* attr = node->fAttr;
   while (attr != nullptr) {
      SXmlAttr_t* next = attr->fNext;
      //fNumNodes--;
      free(attr);
      attr = next;
   }

   free(node);

   //fNumNodes--;
}

////////////////////////////////////////////////////////////////////////////////
/// combined operation. Unlink node and free used memory

void TXMLEngine::UnlinkFreeNode(XMLNodePointer_t xmlnode)
{
   UnlinkNode(xmlnode);
   FreeNode(xmlnode);
}

////////////////////////////////////////////////////////////////////////////////
/// returns name of xmlnode

const char* TXMLEngine::GetNodeName(XMLNodePointer_t xmlnode)
{
   return xmlnode == nullptr ? nullptr : SXmlNode_t::Name(xmlnode);
}

////////////////////////////////////////////////////////////////////////////////
/// get contents (if any) of xml node

const char* TXMLEngine::GetNodeContent(XMLNodePointer_t xmlnode)
{
   if (xmlnode == nullptr) return nullptr;
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;
   if (node->fChild == nullptr) return nullptr;

   if (node->fChild->fType != kXML_CONTENT) return nullptr;

   return SXmlNode_t::Name(node->fChild);
}

////////////////////////////////////////////////////////////////////////////////
/// set content of the xml node
/// if old node content was exists, it will be replaced

void TXMLEngine::SetNodeContent(XMLNodePointer_t xmlnode, const char* content, Int_t len)
{
   if (xmlnode == nullptr) return;
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;
   if ((node->fChild != nullptr) && (node->fChild->fType == kXML_CONTENT))
      UnlinkFreeNode((XMLNodePointer_t) node->fChild);

   if (content == nullptr) return;
   if (len<=0) len = strlen(content);

   SXmlNode_t *contnode = (SXmlNode_t *)AllocateNode(len, nullptr);
   char* nameptr = SXmlNode_t::Name(contnode);
   contnode->fType = kXML_CONTENT;
   strncpy(nameptr, content, len);
   nameptr+=len;
   *nameptr = 0; // here we add padding 0 to get normal string

   AddChildFirst(xmlnode, (XMLNodePointer_t)contnode);
}

////////////////////////////////////////////////////////////////////////////////
/// add new content of the xml node
/// old content will be preserved, one could mix content with child nodes

void TXMLEngine::AddNodeContent(XMLNodePointer_t xmlnode, const char* content, Int_t len)
{
   if ((xmlnode == nullptr) || (content == nullptr)) return;
   if (len<=0) len = strlen(content);

   SXmlNode_t* contnode = (SXmlNode_t*) AllocateNode(len, xmlnode);
   char* nameptr = SXmlNode_t::Name(contnode);
   contnode->fType = kXML_CONTENT;
   strncpy(nameptr, content, len);
   nameptr+=len;
   *nameptr = 0; // here we add padding 0 to get normal string
}

////////////////////////////////////////////////////////////////////////////////
/// returns first child of xml node

XMLNodePointer_t TXMLEngine::GetChild(XMLNodePointer_t xmlnode, Bool_t realnode)
{
   XMLNodePointer_t res = xmlnode == nullptr ? nullptr : ((SXmlNode_t *)xmlnode)->fChild;
   // skip content(s) node, if specified
   if (realnode && (res != nullptr) && (((SXmlNode_t *)res)->fType != kXML_NODE)) ShiftToNext(res, kTRUE);
   return res;
}

////////////////////////////////////////////////////////////////////////////////
/// returns parent of xmlnode

XMLNodePointer_t TXMLEngine::GetParent(XMLNodePointer_t xmlnode)
{
   return xmlnode == nullptr ? nullptr : (XMLNodePointer_t)((SXmlNode_t *)xmlnode)->fParent;
}

////////////////////////////////////////////////////////////////////////////////
/// return next to xmlnode node
/// if realnode==kTRUE, any special nodes in between will be skipped

XMLNodePointer_t TXMLEngine::GetNext(XMLNodePointer_t xmlnode, Bool_t realnode)
{
   do {
      xmlnode = xmlnode == nullptr ? nullptr : (XMLNodePointer_t)((SXmlNode_t *)xmlnode)->fNext;
      if ((xmlnode == nullptr) || !realnode) return xmlnode;
   } while (((SXmlNode_t*) xmlnode)->fType != kXML_NODE);

   return xmlnode;
}

////////////////////////////////////////////////////////////////////////////////
/// shifts specified node to next
/// if realnode==kTRUE, any special nodes in between will be skipped

void TXMLEngine::ShiftToNext(XMLNodePointer_t &xmlnode, Bool_t realnode)
{
   do {
      xmlnode = xmlnode == nullptr ? nullptr : (XMLNodePointer_t)((SXmlNode_t *)xmlnode)->fNext;
      if ((xmlnode == nullptr) || !realnode) return;
   } while (((SXmlNode_t*) xmlnode)->fType != kXML_NODE);
}

////////////////////////////////////////////////////////////////////////////////
/// return kTRUE is this is normal xml node

Bool_t TXMLEngine::IsXmlNode(XMLNodePointer_t xmlnode)
{
   return xmlnode == nullptr ? kFALSE : (((SXmlNode_t *)xmlnode)->fType == kXML_NODE);
}

////////////////////////////////////////////////////////////////////////////////
/// return kTRUE is this is node with special data like comments to data processing instructions

Bool_t TXMLEngine::IsEmptyNode(XMLNodePointer_t xmlnode)
{
   return xmlnode == nullptr ? kTRUE : (((SXmlNode_t *)xmlnode)->fType != kXML_NODE);
}

////////////////////////////////////////////////////////////////////////////////
/// return kTRUE is this is special node with content

Bool_t TXMLEngine::IsContentNode(XMLNodePointer_t xmlnode)
{
   return xmlnode == nullptr ? kFALSE : (((SXmlNode_t *)xmlnode)->fType == kXML_CONTENT);
}

////////////////////////////////////////////////////////////////////////////////
/// return kTRUE is this is special node with content

Bool_t TXMLEngine::IsCommentNode(XMLNodePointer_t xmlnode)
{
   return xmlnode == nullptr ? kFALSE : (((SXmlNode_t *)xmlnode)->fType == kXML_COMMENT);
}


////////////////////////////////////////////////////////////////////////////////
/// Skip all current empty nodes and locate on first "true" node

void TXMLEngine::SkipEmpty(XMLNodePointer_t &xmlnode)
{
   if (IsEmptyNode(xmlnode)) ShiftToNext(xmlnode);
}


////////////////////////////////////////////////////////////////////////////////
/// remove all children node from xmlnode

void TXMLEngine::CleanNode(XMLNodePointer_t xmlnode)
{
   if (xmlnode == nullptr) return;
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;

   SXmlNode_t* child = node->fChild;
   while (child != nullptr) {
      SXmlNode_t* next = child->fNext;
      FreeNode((XMLNodePointer_t)child);
      child = next;
   }

   node->fChild = nullptr;
   node->fLastChild = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// creates new xml document with provided version

XMLDocPointer_t TXMLEngine::NewDoc(const char* version)
{
   SXmlDoc_t* doc = new SXmlDoc_t;
   doc->fRootNode = (SXmlNode_t *)NewChild(nullptr, nullptr, "??DummyTopNode??", nullptr);

   if (version != nullptr) {
      XMLNodePointer_t vernode = NewChild((XMLNodePointer_t)doc->fRootNode, nullptr, "xml");
      ((SXmlNode_t*) vernode)->fType = kXML_PI_NODE;
      NewAttr(vernode, nullptr, "version", version);
   }

   doc->fDtdName = nullptr;
   doc->fDtdRoot = nullptr;
   return (XMLDocPointer_t) doc;
}

////////////////////////////////////////////////////////////////////////////////
/// assignes dtd filename to document

void TXMLEngine::AssignDtd(XMLDocPointer_t xmldoc, const char* dtdname, const char* rootname)
{
   if (xmldoc == nullptr) return;
   SXmlDoc_t* doc = (SXmlDoc_t*) xmldoc;
   delete[] doc->fDtdName;
   doc->fDtdName = Makestr(dtdname);
   delete[] doc->fDtdRoot;
   doc->fDtdRoot = Makestr(rootname);
}

////////////////////////////////////////////////////////////////////////////////
/// frees allocated document data and deletes document itself

void TXMLEngine::FreeDoc(XMLDocPointer_t xmldoc)
{
   if (xmldoc == nullptr) return;
   SXmlDoc_t* doc = (SXmlDoc_t*) xmldoc;
   FreeNode((XMLNodePointer_t) doc->fRootNode);
   delete[] doc->fDtdName;
   delete[] doc->fDtdRoot;
   delete doc;
}

////////////////////////////////////////////////////////////////////////////////
/// store document content to file
/// if layout<=0, no any spaces or newlines will be placed between
/// xmlnodes. Xml file will have minimum size, but nonreadable structure
/// if (layout>0) each node will be started from new line,
/// and number of spaces will correspond to structure depth.

void TXMLEngine::SaveDoc(XMLDocPointer_t xmldoc, const char* filename, Int_t layout)
{
   if (xmldoc == nullptr) return;

   SXmlDoc_t* doc = (SXmlDoc_t*) xmldoc;

   TXMLOutputStream out(filename, 100000);

   XMLNodePointer_t child = GetChild((XMLNodePointer_t) doc->fRootNode, kFALSE);

   do {
      SaveNode(child, &out, layout, 0);
      ShiftToNext(child, kFALSE);
   } while (child != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
/// set main (root) node for document

void TXMLEngine::DocSetRootElement(XMLDocPointer_t xmldoc, XMLNodePointer_t xmlnode)
{
   if (xmldoc == nullptr) return;

   FreeNode(DocGetRootElement(xmldoc));

   AddChild((XMLNodePointer_t) ((SXmlDoc_t*)xmldoc)->fRootNode, xmlnode);
}

////////////////////////////////////////////////////////////////////////////////
/// returns root node of document

XMLNodePointer_t TXMLEngine::DocGetRootElement(XMLDocPointer_t xmldoc)
{
   if (xmldoc == nullptr) return nullptr;

   XMLNodePointer_t xmlnode = (XMLNodePointer_t) ((SXmlDoc_t*)xmldoc)->fRootNode;

   // typically first child of XML document is version
   // therefore just skip it when returning root node of document
   return GetChild(xmlnode, kTRUE);
}

////////////////////////////////////////////////////////////////////////////////
/// Parses content of file and tries to produce xml structures.
/// The maxbuf argument specifies the max size of the XML file to be
/// parsed. The default value is 100000.

XMLDocPointer_t TXMLEngine::ParseFile(const char* filename, Int_t maxbuf)
{
   if ((filename == nullptr) || (strlen(filename) == 0)) return nullptr;
   if (maxbuf < 100000) maxbuf = 100000;
   TXMLInputStream inp(true, filename, maxbuf);
   return ParseStream(&inp);
}

////////////////////////////////////////////////////////////////////////////////
/// parses content of string and tries to produce xml structures

XMLDocPointer_t TXMLEngine::ParseString(const char* xmlstring)
{
   if ((xmlstring == nullptr) || (strlen(xmlstring) == 0)) return nullptr;
   TXMLInputStream inp(false, xmlstring, 2*strlen(xmlstring) );
   return ParseStream(&inp);
}

////////////////////////////////////////////////////////////////////////////////
/// parses content of the stream and tries to produce xml structures

XMLDocPointer_t TXMLEngine::ParseStream(TXMLInputStream* inp)
{
   if (inp == nullptr) return nullptr;

   XMLDocPointer_t xmldoc = NewDoc(nullptr);

   Bool_t success = false;

   Int_t resvalue = 0;

   do {
      ReadNode(((SXmlDoc_t*) xmldoc)->fRootNode, inp, resvalue);

      if (resvalue!=2) break;

      // coverity[unchecked_value] at this place result of SkipSpaces() doesn't matter - either file is finished (false) or there is some more nodes to analyse (true)
      if (!inp->EndOfStream()) inp->SkipSpaces();

      if (inp->EndOfStream()) {
         success = true;
         break;
      }
   } while (true);

   if (!success) {
      DisplayError(resvalue, inp->CurrentLine());
      FreeDoc(xmldoc);
      return nullptr;
   }

   return xmldoc;
}

////////////////////////////////////////////////////////////////////////////////
/// check that first node is xml processing instruction with correct xml version number

Bool_t TXMLEngine::ValidateVersion(XMLDocPointer_t xmldoc, const char* version)
{
   if (xmldoc == nullptr) return kFALSE;

   XMLNodePointer_t vernode = GetChild((XMLNodePointer_t) ((SXmlDoc_t*) xmldoc)->fRootNode, kFALSE);
   if (vernode == nullptr) return kFALSE;

   if (((SXmlNode_t*) vernode)->fType!=kXML_PI_NODE) return kFALSE;
   if (strcmp(GetNodeName(vernode), "xml")!=0) return kFALSE;

   const char* value = GetAttr(vernode,"version");
   if (value == nullptr) return kFALSE;
   if (version == nullptr) version = "1.0";

   return strcmp(version,value)==0;
}

////////////////////////////////////////////////////////////////////////////////
/// convert single xml node (and its child node) to string
/// if layout<=0, no any spaces or newlines will be placed between
/// xmlnodes. Xml file will have minimum size, but nonreadable structure
/// if (layout>0) each node will be started from new line,
/// and number of spaces will correspond to structure depth.

void TXMLEngine::SaveSingleNode(XMLNodePointer_t xmlnode, TString* res, Int_t layout)
{
   if ((res == nullptr) || (xmlnode == nullptr)) return;

   TXMLOutputStream out(res, 10000);

   SaveNode(xmlnode, &out, layout, 0);
}

////////////////////////////////////////////////////////////////////////////////
/// read single xml node from provided string

XMLNodePointer_t TXMLEngine::ReadSingleNode(const char* src)
{
   if (src == nullptr) return nullptr;

   TXMLInputStream inp(false, src, 10000);

   Int_t resvalue;

   XMLNodePointer_t xmlnode = ReadNode(nullptr, &inp, resvalue);

   if (resvalue<=0) {
      DisplayError(resvalue, inp.CurrentLine());
      FreeNode(xmlnode);
      return nullptr;
   }

   return xmlnode;
}

////////////////////////////////////////////////////////////////////////////////
/// creates char* variable with copy of provided string

char* TXMLEngine::Makestr(const char* str)
{
   if (str == nullptr) return nullptr;
   int len = strlen(str);
   if (len == 0) return nullptr;
   char* res = new char[len+1];
   strncpy(res, str, len+1);
   return res;
}

////////////////////////////////////////////////////////////////////////////////
/// creates char* variable with copy of len symbols from provided string

char* TXMLEngine::Makenstr(const char* str, int len)
{
   if ((str == nullptr) || (len == 0)) return nullptr;
   char* res = new char[len+1];
   strncpy(res, str, len);
   *(res+len) = 0;
   return res;
}

////////////////////////////////////////////////////////////////////////////////
/// Allocates new xml node with specified namelength

XMLNodePointer_t TXMLEngine::AllocateNode(int namelen, XMLNodePointer_t parent)
{
   //fNumNodes++;

   SXmlNode_t* node = (SXmlNode_t*) malloc(sizeof(SXmlNode_t) + namelen + 1);

   node->fType = kXML_NODE;
   node->fParent = nullptr;
   node->fNs = nullptr;
   node->fAttr = nullptr;
   node->fChild = nullptr;
   node->fLastChild = nullptr;
   node->fNext = nullptr;

   if (parent != nullptr) AddChild(parent, (XMLNodePointer_t)node);

   return (XMLNodePointer_t) node;
}

////////////////////////////////////////////////////////////////////////////////
/// Allocate new attribute with specified name length and value length

XMLAttrPointer_t TXMLEngine::AllocateAttr(int namelen, int valuelen, XMLNodePointer_t xmlnode)
{
   //fNumNodes++;

   SXmlAttr_t* attr = (SXmlAttr_t*) malloc(sizeof(SXmlAttr_t) + namelen + 1 + valuelen + 1);

   SXmlNode_t* node = (SXmlNode_t*) xmlnode;

   attr->fNext = nullptr;

   if (node->fAttr == nullptr)
      node->fAttr = attr;
   else {
      SXmlAttr_t* d = node->fAttr;
      while (d->fNext != nullptr) d = d->fNext;
      d->fNext = attr;
   }

   return (XMLAttrPointer_t) attr;
}

////////////////////////////////////////////////////////////////////////////////
/// define if namespace of that name exists for xmlnode

XMLNsPointer_t TXMLEngine::FindNs(XMLNodePointer_t xmlnode, const char* name)
{
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;
   while (node != nullptr) {
      if (node->fNs != nullptr) {
         const char* nsname = SXmlAttr_t::Name(node->fNs) + 6;
         if (strcmp(nsname, name)==0) return node->fNs;
      }
      node = node->fParent;
   }
   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// removes namespace extension of nodename

void TXMLEngine::TruncateNsExtension(XMLNodePointer_t xmlnode)
{
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;
   if (node == nullptr) return;
   char* colon = strchr(SXmlNode_t::Name(node),':');
   if (colon == nullptr) return;

   char* copyname = SXmlNode_t::Name(node);

   while (*colon!=0)
     *(copyname++) = *(++colon);
}

////////////////////////////////////////////////////////////////////////////////
/// unpack special symbols, used in xml syntax to code characters
/// these symbols: '<' - &lt, '>' - &gt, '&' - &amp, '"' - &quot, ''' - &apos

void TXMLEngine::UnpackSpecialCharacters(char* target, const char* source, int srclen)
{
   while (srclen>0) {
      if (*source=='&') {
         if ((srclen>3) && (*(source+1)=='l') && (*(source+2)=='t') && (*(source+3)==';')) {
            *target++ = '<'; source+=4; srclen-=4;
         } else
         if ((srclen>3) && (*(source+1)=='g') && (*(source+2)=='t') && (*(source+3)==';')) {
            *target++ = '>'; source+=4; srclen-=4;
         } else
         if ((srclen>4) && (*(source+1)=='a') && (*(source+2)=='m') && (*(source+3)=='p') && (*(source+4)==';')) {
            *target++ = '&'; source+=5; srclen-=5;
         } else
         if ((srclen>5) && (*(source+1)=='q') && (*(source+2)=='u') && (*(source+3)=='o') && (*(source+4)=='t') && (*(source+5)==';')) {
            *target++ = '\"'; source+=6; srclen-=6;
         } else
         if ((srclen>5) && (*(source+1)=='a') && (*(source+2)=='p') && (*(source+3)=='o') && (*(source+4)=='s') && (*(source+5)==';')) {
            *target++ = '\''; source+=6; srclen-=6;
         } else {
            *target++ = *source++; srclen--;
         }
      } else {
         *target++ = *source++;
         srclen--;
      }
   }
   *target = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// output value to output stream
/// if symbols '<' '&' '>' '"' ''' appears in the string, they
/// will be encoded to appropriate xml symbols: &lt, &amp, &gt, &quot, &apos

void TXMLEngine::OutputValue(char* value, TXMLOutputStream* out)
{
   if (value == nullptr) return;

   char* last = value;
   char *find = nullptr;
   while ((find = strpbrk(last, "<&>\"")) != nullptr) {
      char symb = *find;
      *find = 0;
      out->Write(last);
      *find = symb;
      last = find+1;
      if (symb=='<')       out->Write("&lt;");
      else if (symb=='>')  out->Write("&gt;");
      else if (symb=='&')  out->Write("&amp;");
      else if (symb=='\'') out->Write("&apos;");
      else                 out->Write("&quot;");
   }
   if (*last!=0)
      out->Write(last);
}

////////////////////////////////////////////////////////////////////////////////
/// stream data of xmlnode to output

void TXMLEngine::SaveNode(XMLNodePointer_t xmlnode, TXMLOutputStream* out, Int_t layout, Int_t level)
{
   if (xmlnode == nullptr) return;
   SXmlNode_t* node = (SXmlNode_t*) xmlnode;

   Bool_t issingleline = (node->fChild == nullptr);

   if (layout>0) out->Put(' ', level);

   if (node->fType == kXML_COMMENT) {
      out->Write("<!--");
      out->Write(SXmlNode_t::Name(node));
      out->Write("-->");
      if (layout>0) out->Put('\n');
      return;
   } else
   if (node->fType == kXML_RAWLINE) {
      out->Write(SXmlNode_t::Name(node));
      if (layout>0) out->Put('\n');
      return;
   } else
   if (node->fType == kXML_CONTENT) {
      out->Write(SXmlNode_t::Name(node));
      if (layout>0) out->Put('\n');
      return;
   }

   out->Put('<');
   if (node->fType==kXML_PI_NODE) out->Put('?');

   // we suppose that ns is always first attribute
   if ((node->fNs != nullptr) && (node->fNs != node->fAttr)) {
      out->Write(SXmlAttr_t::Name(node->fNs)+6);
      out->Put(':');
   }
   out->Write(SXmlNode_t::Name(node));

   SXmlAttr_t* attr = node->fAttr;
   while (attr != nullptr) {
      out->Put(' ');
      char* attrname = SXmlAttr_t::Name(attr);
      out->Write(attrname);
      out->Write("=\"");
      attrname += strlen(attrname) + 1;
      OutputValue(attrname, out);
      out->Put('\"');
      attr = attr->fNext;
   }

   // if single line, close node with "/>" and return
   if (issingleline) {
      if (node->fType==kXML_PI_NODE) out->Write("?>");
                              else out->Write("/>");
      if (layout>0) out->Put('\n');
      return;
   }

   out->Put('>');

   SXmlNode_t* child = node->fChild;

   if ((child != nullptr) && (child->fType == kXML_CONTENT) && (child->fNext == nullptr)) {
      // special case when single content node is exists
      out->Write(SXmlNode_t::Name(child));
   } else {
      if (layout>0) out->Put('\n');
      while (child != nullptr) {
         SaveNode((XMLNodePointer_t) child, out, layout, level+2);
         child = child->fNext;
      }
      // add starting spaces before closing node
      if (layout>0) out->Put(' ',level);
   }

   out->Write("</");
   // we suppose that ns is always first attribute
   if ((node->fNs != nullptr) && (node->fNs != node->fAttr)) {
      out->Write(SXmlAttr_t::Name(node->fNs)+6);
      out->Put(':');
   }
   out->Write(SXmlNode_t::Name(node));
   out->Put('>');
   if (layout>0) out->Put('\n');
}

////////////////////////////////////////////////////////////////////////////////
/// Tries to construct xml node from input stream. Node should be
/// child of xmlparent node or it can be closing tag of xmlparent.
/// resvalue <= 0 if error
/// resvalue == 1 if this is endnode of parent
/// resvalue == 2 if this is child

XMLNodePointer_t TXMLEngine::ReadNode(XMLNodePointer_t xmlparent, TXMLInputStream* inp, Int_t& resvalue)
{
   resvalue = 0;

   if (inp == nullptr) return nullptr;
   if (!inp->SkipSpaces()) { resvalue = -1;
      return nullptr;
   }
   SXmlNode_t* parent = (SXmlNode_t*) xmlparent;

   SXmlNode_t *node = nullptr;

   // process comments before we start to analyse any node symbols
   while (inp->CheckFor("<!--")) {
      Int_t commentlen = inp->SearchFor("-->");
      if (commentlen<=0) { resvalue = -10;
         return nullptr;
      }

      if (!fSkipComments) {
         node = (SXmlNode_t*) AllocateNode(commentlen, xmlparent);
         char* nameptr = SXmlNode_t::Name(node);
         node->fType = kXML_COMMENT;
         strncpy(nameptr, inp->fCurrent, commentlen); // here copy only content, there is no padding 0 at the end
         nameptr+=commentlen;
         *nameptr = 0; // here we add padding 0 to get normal string
      }

      if (!inp->ShiftCurrent(commentlen+3)) { resvalue = -1; return node; }
      if (!inp->SkipSpaces()) { resvalue = -1; return node; }

      resvalue = 2;
      return node;
   }

   if (*inp->fCurrent!='<') {
      // here should be reading of element content
      // now one can have content at any place of the node, also after childs
      if (parent == nullptr) {
         resvalue = -2;
         return nullptr;
      }
      int contlen = inp->LocateContent();
      if (contlen < 0) return nullptr;

      SXmlNode_t* contnode = (SXmlNode_t*) AllocateNode(contlen, xmlparent);
      contnode->fType = kXML_CONTENT;
      char* contptr = SXmlNode_t::Name(contnode);
      UnpackSpecialCharacters(contptr, inp->fCurrent, contlen);
      if (!inp->ShiftCurrent(contlen)) return nullptr;

      if (inp->NumEntities() <= 0) {
         resvalue = 2;
         return contnode;
      }

      // analyze content on possible includes only when ENTITY was specified for document

      const char *beg(nullptr), *lastentity(nullptr), *curr(contptr);

      while (*curr != 0) {
         if ((beg == nullptr) && (*curr == '&')) beg = curr;
         if ((beg == nullptr) || (*curr != ';')) {
            curr++;
            continue;
         }

         TXMLEntity* entity = inp->FindEntity(beg+1, curr - beg - 1);

         if (entity != nullptr) {

            if (lastentity == nullptr) {
               lastentity = contptr;
               UnlinkNode(contnode);
            }

            if (lastentity!=beg)
               AddNodeContent(xmlparent, lastentity, beg - lastentity);

            // printf("Find entity %s in content\n", entity->GetName());
            if (entity->IsSystem()) {
               XMLDocPointer_t entitydoc = ParseFile(entity->GetTitle());
               if (entitydoc == nullptr) {
                  resvalue = -14;
                  return contnode;
               }

               XMLNodePointer_t topnode = DocGetRootElement(entitydoc);

               while (topnode != nullptr) {
                  XMLNodePointer_t currnode = topnode;
                  ShiftToNext(topnode, false);
                  UnlinkNode(currnode);
                  AddChild(xmlparent, currnode);
               }
            } else {
               AddNodeContent(xmlparent, entity->GetTitle());
            }
         }

         beg = nullptr;
         curr++;

         lastentity = curr;
      }

      if (lastentity != nullptr) {
         // add rest part of the content
         if (strlen(lastentity)>0) AddNodeContent(xmlparent,lastentity);
         // do not forget to cleanup content node
         FreeNode(contnode);
         contnode = nullptr;
      }

      resvalue = 2;
      return contnode;
   } else {
      // skip "<" symbol
      if (!inp->ShiftCurrent()) return nullptr;
   }

   if (*inp->fCurrent=='/') {
      // this is a starting of closing node
      if (!inp->ShiftCurrent()) return nullptr;
      if (!inp->SkipSpaces()) return nullptr;
      Int_t len = inp->LocateIdentifier();
      if (len<=0) { resvalue = -3;
         return nullptr;
      }

      if (parent == nullptr) {
         resvalue = -4;
         return nullptr;
      }

      if (strncmp(SXmlNode_t::Name(parent), inp->fCurrent, len)!=0) {
         resvalue = -5;
         return nullptr;
      }

      if (!inp->ShiftCurrent(len)) return nullptr;

      if (!inp->SkipSpaces()) return nullptr;
      if (*inp->fCurrent != '>') return nullptr;
      if (!inp->ShiftCurrent()) return nullptr;

      if (parent->fNs != nullptr) TruncateNsExtension((XMLNodePointer_t)parent);

      inp->SkipSpaces(kTRUE); // locate start of next string
      resvalue = 1;
      return nullptr;
   }

   if (*inp->fCurrent=='!') {
      // this is start of DTD reading, only limited number of features are supported
      if (!inp->ShiftCurrent()) return nullptr;
      if (!inp->CheckFor("DOCTYPE")) { resvalue = -12;
         return nullptr;
      }
      if (!inp->SkipSpaces()) { resvalue = -13;
         return nullptr;
      }

      // now skip name of the root element - it is not verified at all
      Int_t len = inp->LocateIdentifier();
      if (len<=0) { resvalue = -13;
         return nullptr;
      }
      if (!inp->ShiftCurrent(len)) { resvalue = -13;
         return nullptr;
      }
      if (!inp->SkipSpaces()) { resvalue = -13;
         return nullptr;
      }

      // this is start of reading ENTITIES
      if (inp->CheckFor("[")) {
         if (!inp->SkipSpaces()) return nullptr;
         while (true) {
            if (inp->CheckFor("<!ENTITY")) {
               // process ENTITY from DTD
               if (!inp->SkipSpaces()) { resvalue = -13;
                  return nullptr;
               }
               Int_t namelen = inp->LocateIdentifier();
               if (namelen<=0) { resvalue = -13;
                  return nullptr;
               }
               TString entity_name(inp->fCurrent, namelen);
               if (!inp->ShiftCurrent(namelen)) { resvalue = -13;
                  return nullptr;
               }
               if (!inp->SkipSpaces()) { resvalue = -13;
                  return nullptr;
               }
               Bool_t is_system = kFALSE;
               if (inp->CheckFor("SYSTEM")) {
                  if (!inp->SkipSpaces()) { resvalue = -13;
                     return nullptr;
                  }
                  is_system = kTRUE;
               }

               char* valuestart = inp->fCurrent;
               Int_t valuelen = inp->LocateValue(valuestart, false);
               if (valuelen < 2) { resvalue = -13;
                  return nullptr;
               }

               TString entity_value(valuestart+1, valuelen-2);

               if (!inp->ShiftCurrent(valuelen)) { resvalue = -13;
                  return nullptr;
               }
               inp->SkipSpaces();
               if (*inp->fCurrent!='>') { resvalue = -13;
                  return nullptr;
               }
               if (!inp->ShiftCurrent()) { resvalue = -13;
                  return nullptr;
               }
               inp->SkipSpaces();

               inp->AddEntity(new TXMLEntity(entity_name, entity_value, is_system));
               continue;

               // printf("Entity:%s system:%s value:%s\n", entity_name.Data(), is_system ? "true" : "false", entity_value.Data());
            }

            if (inp->CheckFor("<!ELEMENT")) {
               // process ELEMENT from DTD - dummy at the moment
               if (!inp->SkipSpaces()) { resvalue = -13;
                  return nullptr;
               }
               Int_t namelen = inp->LocateIdentifier();
               if (namelen<=0) { resvalue = -13;
                  return nullptr;
               }

               if (!inp->ShiftCurrent(namelen)) { resvalue = -13;
                  return nullptr;
               }
               if (!inp->SkipSpaces()) { resvalue = -13;
                  return nullptr;
               }

               if (!inp->CheckFor("(")) { resvalue = -13;
                  return nullptr;
               }
               if (inp->SearchFor(")")<=0) { resvalue = -13;
                  return nullptr;
               }

               inp->SkipSpaces();
               if (*inp->fCurrent!='>') { resvalue = -13;
                  return nullptr;
               }
               if (!inp->ShiftCurrent()) { resvalue = -13;
                  return nullptr;
               }
               inp->SkipSpaces();

               continue;
            }

            break;
         }

         if (!inp->CheckFor("]")) { resvalue = -13;
            return nullptr;
         }
      }
      inp->SkipSpaces();
      if (!inp->CheckFor(">")) { resvalue = -13;
         return nullptr;
      }

      resvalue = 2;
      return node;
   }



   EXmlNodeType nodetype = kXML_NODE;
   Bool_t canhaschildren = true;
   char endsymbol = '/';

   // this is case of processing instructions node
   if (*inp->fCurrent=='?') {
      if (!inp->ShiftCurrent()) return nullptr;
      nodetype = kXML_PI_NODE;
      canhaschildren = false;
      endsymbol = '?';
   }

   if (!inp->SkipSpaces()) return nullptr;
   Int_t len = inp->LocateIdentifier();
   if (len <= 0) return nullptr;
   node = (SXmlNode_t*) AllocateNode(len, xmlparent);
   char* nameptr = SXmlNode_t::Name(node);
   node->fType = nodetype;

   strncpy(nameptr, inp->fCurrent, len); // here copied content without padding 0
   nameptr+=len;
   *nameptr = 0; // add 0 to the end

   char* colon = strchr(SXmlNode_t::Name(node),':');
   if ((colon != nullptr) && (parent != nullptr)) {
      *colon = 0;
      node->fNs = (SXmlAttr_t*) FindNs(xmlparent, SXmlNode_t::Name(node));
      *colon =':';
   }

   if (!inp->ShiftCurrent(len)) return nullptr;

   do {
      if (!inp->SkipSpaces()) return nullptr;

      char nextsymb = *inp->fCurrent;

      if (nextsymb==endsymbol) {  // this is end of short node like <node ... />
         if (!inp->ShiftCurrent()) return nullptr;
         if (*inp->fCurrent=='>') {
            if (!inp->ShiftCurrent()) return nullptr;

            if (node->fNs != nullptr) TruncateNsExtension((XMLNodePointer_t)node);

            inp->SkipSpaces(kTRUE); // locate start of next string
            resvalue = 2;
            return node;
         } else
            return nullptr;
      } else
      if (nextsymb=='>') { // this is end of parent node, lets find all children
         if (!canhaschildren) { resvalue = -11;
            return nullptr;
         }

         if (!inp->ShiftCurrent()) return nullptr;

         do {
            ReadNode(node, inp, resvalue);
         } while (resvalue==2);

         if (resvalue==1) {
            resvalue = 2;
            return node;
         } else
            return nullptr;
      } else {
         Int_t attrlen = inp->LocateIdentifier();
         if (attrlen<=0) { resvalue = -6;
            return nullptr;
         }

         char* valuestart = inp->fCurrent+attrlen;

         int valuelen = inp->LocateValue(valuestart, true);
         if (valuelen<3) { resvalue = -7;
            return nullptr;
         }

         SXmlAttr_t* attr = (SXmlAttr_t*) AllocateAttr(attrlen, valuelen-3, (XMLNodePointer_t) node);

         char* attrname = SXmlAttr_t::Name(attr);
         strncpy(attrname, inp->fCurrent, attrlen);
         attrname+=attrlen;
         *attrname = 0;
         attrname++;
         UnpackSpecialCharacters(attrname, valuestart+2, valuelen-3);

         if (!inp->ShiftCurrent(attrlen + valuelen)) return nullptr;

         attrname = SXmlAttr_t::Name(attr);

         if ((strlen(attrname)>6) && (strstr(attrname,"xmlns:")==attrname)) {
            if (strcmp(SXmlNode_t::Name(node), attrname + 6)!=0) {
               resvalue = -8;
               //return 0;
            }
            if (node->fNs != nullptr) {
               resvalue = -9;
               //return 0;
            }
            node->fNs = attr;
         }
      }
   } while (true);

   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Displays xml parsing error

void TXMLEngine::DisplayError(Int_t error, Int_t linenumber)
{
   switch(error) {
      case -14: Error("ParseFile", "Error include external XML file at line %d", linenumber); break;
      case -13: Error("ParseFile", "Error processing DTD part of XML file at line %d", linenumber); break;
      case -12: Error("ParseFile", "DOCTYPE missing after <! at line %d", linenumber); break;
      case -11: Error("ParseFile", "Node cannot be closed with > symbol at line %d, for instance <?xml ... ?> node", linenumber); break;
      case -10: Error("ParseFile", "Error in xml comments definition at line %d, must be <!-- comments -->", linenumber); break;
      case -9: Error("ParseFile", "Multiple name space definitions not allowed, line %d", linenumber); break;
      case -8: Error("ParseFile", "Invalid namespace specification, line %d", linenumber); break;
      case -7: Error("ParseFile", "Invalid attribute value, line %d", linenumber); break;
      case -6: Error("ParseFile", "Invalid identifier for node attribute, line %d", linenumber); break;
      case -5: Error("ParseFile", "Mismatch between open and close nodes, line %d", linenumber); break;
      case -4: Error("ParseFile", "Unexpected close node, line %d", linenumber); break;
      case -3: Error("ParseFile", "Valid identifier for close node is missing, line %d", linenumber); break;
      case -2: Error("ParseFile", "No multiple content entries allowed, line %d", linenumber); break;
      case -1: Error("ParseFile", "Unexpected end of xml file"); break;
      default: Error("ParseFile", "XML syntax error at line %d", linenumber); break;
   }

}
