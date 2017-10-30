//===--- MultiplexExternalASTSource.cpp  ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the event dispatching to the subscribed clients.
//
//===----------------------------------------------------------------------===//
#include "MultiplexExternalASTSource.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/Sema/Lookup.h"

using namespace clang;

///\brief Constructs a new multiplexing external sema source and appends the
/// given element to it.
///
///\param[in] source - An ExternalASTSource.
///
MultiplexExternalASTSource::MultiplexExternalASTSource(ExternalASTSource &s1,
                                                        ExternalASTSource &s2){
  Sources.push_back(&s1);
  Sources.push_back(&s2);
}

// pin the vtable here.
MultiplexExternalASTSource::~MultiplexExternalASTSource() {}

///\brief Appends new source to the source list.
///
///\param[in] source - An ExternalASTSource.
///
void MultiplexExternalASTSource::addSource(ExternalASTSource &source) {
  Sources.push_back(&source);
}

//===----------------------------------------------------------------------===//
// ExternalASTSource.
//===----------------------------------------------------------------------===//

Decl *MultiplexExternalASTSource::GetExternalDecl(uint32_t ID) {
  for(size_t i = 0; i < Sources.size(); ++i)
    if (Decl *Result = Sources[i]->GetExternalDecl(ID))
      return Result;
  return nullptr;
}

void MultiplexExternalASTSource::CompleteRedeclChain(const Decl *D) {
  for (size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->CompleteRedeclChain(D);
}

Selector MultiplexExternalASTSource::GetExternalSelector(uint32_t ID) {
  Selector Sel;
  for(size_t i = 0; i < Sources.size(); ++i) {
    Sel = Sources[i]->GetExternalSelector(ID);
    if (!Sel.isNull())
      return Sel;
  }
  return Sel;
}

uint32_t MultiplexExternalASTSource::GetNumExternalSelectors() {
  uint32_t total = 0;
  for(size_t i = 0; i < Sources.size(); ++i)
    total += Sources[i]->GetNumExternalSelectors();
  return total;
}

Stmt *MultiplexExternalASTSource::GetExternalDeclStmt(uint64_t Offset) {
  for(size_t i = 0; i < Sources.size(); ++i)
    if (Stmt *Result = Sources[i]->GetExternalDeclStmt(Offset))
      return Result;
  return nullptr;
}

CXXBaseSpecifier *MultiplexExternalASTSource::GetExternalCXXBaseSpecifiers(
                                                               uint64_t Offset){
  for(size_t i = 0; i < Sources.size(); ++i)
    if (CXXBaseSpecifier *R = Sources[i]->GetExternalCXXBaseSpecifiers(Offset))
      return R;
  return nullptr;
}

CXXCtorInitializer **
MultiplexExternalASTSource::GetExternalCXXCtorInitializers(uint64_t Offset) {
  for (auto *S : Sources)
    if (auto *R = S->GetExternalCXXCtorInitializers(Offset))
      return R;
  return nullptr;
}

ExternalASTSource::ExtKind
MultiplexExternalASTSource::hasExternalDefinitions(const Decl *D) {
  for (const auto &S : Sources)
    if (auto EK = S->hasExternalDefinitions(D))
      if (EK != EK_ReplyHazy)
        return EK;
  return EK_ReplyHazy;
}

bool MultiplexExternalASTSource::
FindExternalVisibleDeclsByName(const DeclContext *DC, DeclarationName Name) {
  bool AnyDeclsFound = false;
  for (size_t i = 0; i < Sources.size(); ++i)
    AnyDeclsFound |= Sources[i]->FindExternalVisibleDeclsByName(DC, Name);
  return AnyDeclsFound;
}

void MultiplexExternalASTSource::completeVisibleDeclsMap(const DeclContext *DC){
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->completeVisibleDeclsMap(DC);
}

void MultiplexExternalASTSource::FindExternalLexicalDecls(
    const DeclContext *DC, llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
    SmallVectorImpl<Decl *> &Result) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->FindExternalLexicalDecls(DC, IsKindWeWant, Result);
}

void MultiplexExternalASTSource::FindFileRegionDecls(FileID File,
                                                      unsigned Offset,
                                                      unsigned Length,
                                                SmallVectorImpl<Decl *> &Decls){
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->FindFileRegionDecls(File, Offset, Length, Decls);
}

void MultiplexExternalASTSource::CompleteType(TagDecl *Tag) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->CompleteType(Tag);
}

void MultiplexExternalASTSource::CompleteType(ObjCInterfaceDecl *Class) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->CompleteType(Class);
}

void MultiplexExternalASTSource::ReadComments() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->ReadComments();
}

void MultiplexExternalASTSource::StartedDeserializing() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->StartedDeserializing();
}

void MultiplexExternalASTSource::FinishedDeserializing() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->FinishedDeserializing();
}

void MultiplexExternalASTSource::StartTranslationUnit(ASTConsumer *Consumer) {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->StartTranslationUnit(Consumer);
}

void MultiplexExternalASTSource::PrintStats() {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->PrintStats();
}

bool MultiplexExternalASTSource::layoutRecordType(const RecordDecl *Record,
                                                   uint64_t &Size, 
                                                   uint64_t &Alignment,
                      llvm::DenseMap<const FieldDecl *, uint64_t> &FieldOffsets,
                  llvm::DenseMap<const CXXRecordDecl *, CharUnits> &BaseOffsets,
          llvm::DenseMap<const CXXRecordDecl *, CharUnits> &VirtualBaseOffsets){
  for(size_t i = 0; i < Sources.size(); ++i)
    if (Sources[i]->layoutRecordType(Record, Size, Alignment, FieldOffsets, 
                                     BaseOffsets, VirtualBaseOffsets))
      return true;
  return false;
}

void MultiplexExternalASTSource::
getMemoryBufferSizes(MemoryBufferSizes &sizes) const {
  for(size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->getMemoryBufferSizes(sizes);

}
