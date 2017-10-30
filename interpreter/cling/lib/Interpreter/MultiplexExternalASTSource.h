//===--- MultiplexExternalASTSource.h - External Sema Interface-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines ExternalASTSource interface, dispatching to all clients
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_SEMA_MultiplexExternalASTSource_H
#define LLVM_CLANG_SEMA_MultiplexExternalASTSource_H

#include "clang/AST/ExternalASTSource.h"
#include "clang/Sema/Weak.h"
#include "llvm/ADT/SmallVector.h"
#include <utility>

namespace clang {

  class CXXConstructorDecl;
  class CXXRecordDecl;
  class DeclaratorDecl;
  struct ExternalVTableUse;
  class LookupResult;
  class NamespaceDecl;
  class Scope;
  class Sema;
  class TypedefNameDecl;
  class ValueDecl;
  class VarDecl;


/// \brief An abstract interface that should be implemented by
/// external AST sources that also provide information for semantic
/// analysis.
class MultiplexExternalASTSource : public ExternalASTSource {

private:
  SmallVector<ExternalASTSource *, 2> Sources; // doesn't own them.

public:
  
  ///\brief Constructs a new multiplexing external sema source and appends the
  /// given element to it.
  ///
  ///\param[in] s1 - A non-null (old) ExternalASTSource.
  ///\param[in] s2 - A non-null (new) ExternalASTSource.
  ///
  MultiplexExternalASTSource(ExternalASTSource &s1, ExternalASTSource &s2);

  ~MultiplexExternalASTSource() override;

  ///\brief Appends new source to the source list.
  ///
  ///\param[in] source - An ExternalASTSource.
  ///
  void addSource(ExternalASTSource &source);

  //===--------------------------------------------------------------------===//
  // ExternalASTSource.
  //===--------------------------------------------------------------------===//

  /// \brief Resolve a declaration ID into a declaration, potentially
  /// building a new declaration.
  Decl *GetExternalDecl(uint32_t ID) override;

  /// \brief Complete the redeclaration chain if it's been extended since the
  /// previous generation of the AST source.
  void CompleteRedeclChain(const Decl *D) override;

  /// \brief Resolve a selector ID into a selector.
  Selector GetExternalSelector(uint32_t ID) override;

  /// \brief Returns the number of selectors known to the external AST
  /// source.
  uint32_t GetNumExternalSelectors() override;

  /// \brief Resolve the offset of a statement in the decl stream into
  /// a statement.
  Stmt *GetExternalDeclStmt(uint64_t Offset) override;

  /// \brief Resolve the offset of a set of C++ base specifiers in the decl
  /// stream into an array of specifiers.
  CXXBaseSpecifier *GetExternalCXXBaseSpecifiers(uint64_t Offset) override;

  /// \brief Resolve a handle to a list of ctor initializers into the list of
  /// initializers themselves.
  CXXCtorInitializer **GetExternalCXXCtorInitializers(uint64_t Offset) override;

  ExtKind hasExternalDefinitions(const Decl *D) override;

  /// \brief Find all declarations with the given name in the
  /// given context.
  bool FindExternalVisibleDeclsByName(const DeclContext *DC,
                                      DeclarationName Name) override;

  /// \brief Ensures that the table of all visible declarations inside this
  /// context is up to date.
  void completeVisibleDeclsMap(const DeclContext *DC) override;

  /// \brief Finds all declarations lexically contained within the given
  /// DeclContext, after applying an optional filter predicate.
  ///
  /// \param IsKindWeWant a predicate function that returns true if the passed
  /// declaration kind is one we are looking for.
  void
  FindExternalLexicalDecls(const DeclContext *DC,
                           llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
                           SmallVectorImpl<Decl *> &Result) override;

  /// \brief Get the decls that are contained in a file in the Offset/Length
  /// range. \p Length can be 0 to indicate a point at \p Offset instead of
  /// a range. 
  void FindFileRegionDecls(FileID File, unsigned Offset,unsigned Length,
                           SmallVectorImpl<Decl *> &Decls) override;

  /// \brief Gives the external AST source an opportunity to complete
  /// an incomplete type.
  void CompleteType(TagDecl *Tag) override;

  /// \brief Gives the external AST source an opportunity to complete an
  /// incomplete Objective-C class.
  ///
  /// This routine will only be invoked if the "externally completed" bit is
  /// set on the ObjCInterfaceDecl via the function 
  /// \c ObjCInterfaceDecl::setExternallyCompleted().
  void CompleteType(ObjCInterfaceDecl *Class) override;

  /// \brief Loads comment ranges.
  void ReadComments() override;

  /// \brief Notify ExternalASTSource that we started deserialization of
  /// a decl or type so until FinishedDeserializing is called there may be
  /// decls that are initializing. Must be paired with FinishedDeserializing.
  void StartedDeserializing() override;

  /// \brief Notify ExternalASTSource that we finished the deserialization of
  /// a decl or type. Must be paired with StartedDeserializing.
  void FinishedDeserializing() override;

  /// \brief Function that will be invoked when we begin parsing a new
  /// translation unit involving this external AST source.
  void StartTranslationUnit(ASTConsumer *Consumer) override;

  /// \brief Print any statistics that have been gathered regarding
  /// the external AST source.
  void PrintStats() override;

  Module *getModule(unsigned ID) override {
    for(size_t i = 0; i < Sources.size(); ++i)
      if (auto M = Sources[i]->getModule(ID))
        return M;
    return nullptr;
  }
  
  /// \brief Perform layout on the given record.
  ///
  /// This routine allows the external AST source to provide an specific 
  /// layout for a record, overriding the layout that would normally be
  /// constructed. It is intended for clients who receive specific layout
  /// details rather than source code (such as LLDB). The client is expected
  /// to fill in the field offsets, base offsets, virtual base offsets, and
  /// complete object size.
  ///
  /// \param Record The record whose layout is being requested.
  ///
  /// \param Size The final size of the record, in bits.
  ///
  /// \param Alignment The final alignment of the record, in bits.
  ///
  /// \param FieldOffsets The offset of each of the fields within the record,
  /// expressed in bits. All of the fields must be provided with offsets.
  ///
  /// \param BaseOffsets The offset of each of the direct, non-virtual base
  /// classes. If any bases are not given offsets, the bases will be laid 
  /// out according to the ABI.
  ///
  /// \param VirtualBaseOffsets The offset of each of the virtual base classes
  /// (either direct or not). If any bases are not given offsets, the bases will 
  /// be laid out according to the ABI.
  /// 
  /// \returns true if the record layout was provided, false otherwise.
  bool
  layoutRecordType(const RecordDecl *Record,
                   uint64_t &Size, uint64_t &Alignment,
                   llvm::DenseMap<const FieldDecl *, uint64_t> &FieldOffsets,
                 llvm::DenseMap<const CXXRecordDecl *, CharUnits> &BaseOffsets,
                 llvm::DenseMap<const CXXRecordDecl *,
                                CharUnits> &VirtualBaseOffsets) override;

  /// Return the amount of memory used by memory buffers, breaking down
  /// by heap-backed versus mmap'ed memory.
  void getMemoryBufferSizes(MemoryBufferSizes &sizes) const override;

  // isa/cast/dyn_cast support
  static bool classof(const MultiplexExternalASTSource*) { return true; }
  //static bool classof(const ExternalASTSource*) { return true; }
}; 

} // end namespace clang

#endif
