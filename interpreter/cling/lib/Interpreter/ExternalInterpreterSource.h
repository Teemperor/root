//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Elisavet Sakellari <elisavet.sakellari@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_EXTERNAL_INTERPRETER_SOURCE
#define CLING_EXTERNAL_INTERPRETER_SOURCE

#include "clang/AST/ExternalASTSource.h"

#include <string>
#include <map>

namespace clang {
  class ASTContext;
  class ASTImporter;
  class Decl;
  class DeclContext;
  class DeclarationName;
  class ExternalASTSource;
  class NamedDecl;
  class Sema;
}

namespace cling {
  class Interpreter;
}

namespace cling {

    class ExternalInterpreterSource : public clang::ExternalASTSource {

      private:
        const cling::Interpreter *m_ParentInterpreter;
        cling::Interpreter *m_ChildInterpreter;

        ///\brief We keep a mapping between the imported DeclContexts
        /// and the original ones from of the first Interpreter.
        /// Key: imported DeclContext
        /// Value: original DeclContext
        ///
        std::map<const clang::DeclContext *, clang::DeclContext *> m_ImportedDeclContexts;

        ///\brief A map for all the imported Decls (Contexts)
        /// according to their names.
        /// Key: Name of the Decl(Context) as a string.
        /// Value: The DeclarationName of this Decl(Context) is the one
        /// that comes from the first Interpreter.
        ///
        std::map <clang::DeclarationName, clang::DeclarationName > m_ImportedDecls;

        ///\brief The ASTImporter which does the actual imports from the parent
        /// interpreter to the child interpreter.
        std::unique_ptr<clang::ASTImporter> m_Importer;

        llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> m_Fallback;

      public:
        ExternalInterpreterSource(const cling::Interpreter *parent,
                                  cling::Interpreter *child, llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> m_Fallback);
        virtual ~ExternalInterpreterSource();

        void completeVisibleDeclsMap(const clang::DeclContext *DC) override;

        bool FindExternalVisibleDeclsByName(
                              const clang::DeclContext *childCurrentDeclContext,
                              clang::DeclarationName childDeclName) override;

        bool Import(clang::DeclContext::lookup_result lookupResult,
                    const clang::DeclContext *childCurrentDeclContext,
                    clang::DeclarationName &childDeclName,
                    clang::DeclarationName &parentDeclName);

        void ImportDeclContext(clang::DeclContext *declContextToImport,
                               clang::DeclarationName &childDeclName,
                               clang::DeclarationName &parentDeclName,
                               const clang::DeclContext *childCurrentDeclContext);

        void ImportDecl(clang::Decl *declToImport,
                        clang::DeclarationName &childDeclName,
                        clang::DeclarationName &parentDeclName,
                        const clang::DeclContext *childCurrentDeclContext);

        void addToImportedDecls(clang::DeclarationName child,
                                clang::DeclarationName parent) {
          m_ImportedDecls[child] = parent;
        }

        void addToImportedDeclContexts(clang::DeclContext *child,
                              clang::DeclContext *parent) {
          m_ImportedDeclContexts[child] = parent;
        }

        //-----------------------
        // Fallback overwrites:
        //-----------------------

        virtual clang::Decl *GetExternalDecl(uint32_t ID) override {
          if (m_Fallback)
            return m_Fallback->GetExternalDecl(ID);
          return ExternalASTSource::GetExternalDecl(ID);
        }

        virtual clang::Selector GetExternalSelector(uint32_t ID) override {
          if (m_Fallback)
            return m_Fallback->GetExternalSelector(ID);
          return ExternalASTSource::GetExternalSelector(ID);
        }

        virtual uint32_t GetNumExternalSelectors() override {
          if (m_Fallback)
            return m_Fallback->GetNumExternalSelectors();
          return ExternalASTSource::GetNumExternalSelectors();
        }

        virtual clang::Stmt *GetExternalDeclStmt(uint64_t Offset) override {
          if (m_Fallback)
            return m_Fallback->GetExternalDeclStmt(Offset);
          return ExternalASTSource::GetExternalDeclStmt(Offset);
        }


        virtual clang::CXXCtorInitializer **GetExternalCXXCtorInitializers(uint64_t Offset) override {
          if (m_Fallback)
            return m_Fallback->GetExternalCXXCtorInitializers(Offset);
          return ExternalASTSource::GetExternalCXXCtorInitializers(Offset);
        }

        virtual clang::CXXBaseSpecifier *GetExternalCXXBaseSpecifiers(uint64_t Offset) override {
          if (m_Fallback)
            return m_Fallback->GetExternalCXXBaseSpecifiers(Offset);
          return ExternalASTSource::GetExternalCXXBaseSpecifiers(Offset);
        }

        virtual void updateOutOfDateIdentifier(clang::IdentifierInfo &II) override {
          if (m_Fallback)
            return m_Fallback->updateOutOfDateIdentifier(II);
          return ExternalASTSource::updateOutOfDateIdentifier(II);
        }

        /// \brief Retrieve the module that corresponds to the given module ID.
        virtual clang::Module *getModule(unsigned ID) override {
          if (m_Fallback)
            return m_Fallback->getModule(ID);
          return ExternalASTSource::getModule(ID);
        }

        /// Return a descriptor for the corresponding module, if one exists.
        virtual llvm::Optional<ASTSourceDescriptor> getSourceDescriptor(unsigned ID) override {
          if (m_Fallback)
            return m_Fallback->getSourceDescriptor(ID);
          return ExternalASTSource::getSourceDescriptor(ID);
        }


        virtual ExtKind hasExternalDefinitions(const clang::Decl *D) override {
          if (m_Fallback)
            return m_Fallback->hasExternalDefinitions(D);
          return ExternalASTSource::hasExternalDefinitions(D);
        }

        virtual void
        FindExternalLexicalDecls(const clang::DeclContext *DC,
                                 llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
                                 llvm::SmallVectorImpl<clang::Decl *> &Result) override {
          if (m_Fallback)
            return m_Fallback->FindExternalLexicalDecls(DC, IsKindWeWant, Result);
          return ExternalASTSource::FindExternalLexicalDecls(DC, IsKindWeWant, Result);
        }

        virtual void FindFileRegionDecls(clang::FileID File, unsigned Offset,
                                         unsigned Length,
                                         llvm::SmallVectorImpl<clang::Decl *> &Decls) override {
          if (m_Fallback)
            return m_Fallback->FindFileRegionDecls(File, Offset, Length, Decls);
          return ExternalASTSource::FindFileRegionDecls(File, Offset, Length, Decls);
        }

        virtual void CompleteRedeclChain(const clang::Decl *D) override {
          if (m_Fallback)
            return m_Fallback->CompleteRedeclChain(D);
          return ExternalASTSource::CompleteRedeclChain(D);
        }

        virtual void CompleteType(clang::TagDecl *Tag) override {
          if (m_Fallback)
            return m_Fallback->CompleteType(Tag);
          return ExternalASTSource::CompleteType(Tag);
        }

        virtual void CompleteType(clang::ObjCInterfaceDecl *Class) override {
          if (m_Fallback)
            return m_Fallback->CompleteType(Class);
          return ExternalASTSource::CompleteType(Class);
        }


        virtual void ReadComments() override {
          if (m_Fallback)
            return m_Fallback->ReadComments();
          return ExternalASTSource::ReadComments();
        }


        virtual void StartedDeserializing() override {
          if (m_Fallback)
            return m_Fallback->StartedDeserializing();
          return ExternalASTSource::StartedDeserializing();
        }

        virtual void FinishedDeserializing() override {
          if (m_Fallback)
            return m_Fallback->FinishedDeserializing();
          return ExternalASTSource::FinishedDeserializing();
        }

        virtual void StartTranslationUnit(clang::ASTConsumer *Consumer) override {
          if (m_Fallback)
            return m_Fallback->StartTranslationUnit(Consumer);
          return ExternalASTSource::StartTranslationUnit(Consumer);
        }

        virtual void PrintStats() override {
          if (m_Fallback)
            return m_Fallback->PrintStats();
          return ExternalASTSource::PrintStats();
        }

        virtual bool layoutRecordType(
            const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
            llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
            llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &BaseOffsets,
            llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &VirtualBaseOffsets) override {
          if (m_Fallback)
            return m_Fallback->layoutRecordType(Record, Size, Alignment, FieldOffsets, BaseOffsets, VirtualBaseOffsets);
          return ExternalASTSource::layoutRecordType(Record, Size, Alignment, FieldOffsets, BaseOffsets, VirtualBaseOffsets);
        }

    };
} // end namespace cling

#endif //CLING_ASTIMPORTSOURCE_H
