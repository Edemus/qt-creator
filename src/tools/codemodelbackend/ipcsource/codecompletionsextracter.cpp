/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://www.qt.io/licensing.  For further information
** use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file.  Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "codecompletionsextracter.h"

#include "clangstring.h"

#ifdef CODEMODELBACKEND_TESTS
#include <gtest/gtest.h>
#endif

#include <QDebug>

namespace CodeModelBackEnd {

CodeCompletionsExtracter::CodeCompletionsExtracter(CXCodeCompleteResults *cxCodeCompleteResults)
    : cxCodeCompleteResults(cxCodeCompleteResults)
{

}

bool CodeCompletionsExtracter::next() const
{
    const uint cxCodeCompleteResultCount = cxCodeCompleteResults->NumResults;

    ++cxCodeCompleteResultIndex;

    if (cxCodeCompleteResultIndex < cxCodeCompleteResultCount) {
        currentCxCodeCompleteResult = cxCodeCompleteResults->Results[cxCodeCompleteResultIndex];

        currentCodeCompletion_ = CodeCompletion();

        extractCompletionKind();
        extractText();
        extractPriority();
        extractAvailability();
        extractHasParameters();

        return true;
    }

    return false;
}

bool CodeCompletionsExtracter::peek(const Utf8String &name) const
{
    const uint cxCodeCompleteResultCount = cxCodeCompleteResults->NumResults;

    uint peekCxCodeCompleteResultIndex = cxCodeCompleteResultIndex + 1;

    while (peekCxCodeCompleteResultIndex < cxCodeCompleteResultCount) {
        if (hasText(name, cxCodeCompleteResults->Results[peekCxCodeCompleteResultIndex].CompletionString))
            return true;

        ++peekCxCodeCompleteResultIndex;
    }

    return false;
}

const QVector<CodeCompletion> CodeCompletionsExtracter::extractAll()
{
    QVector<CodeCompletion> codeCompletions;

    while(next())
        codeCompletions.append(currentCodeCompletion_);

    return codeCompletions;
}

void CodeCompletionsExtracter::extractCompletionKind() const
{
    switch (currentCxCodeCompleteResult.CursorKind) {
        case CXCursor_FunctionTemplate:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::TemplateFunctionCompletionKind);
            break;
        case CXCursor_CXXMethod:
            extractMethodCompletionKind();
            break;
        case CXCursor_FunctionDecl:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::FunctionCompletionKind);
            break;
        case CXCursor_VariableRef:
        case CXCursor_VarDecl:
        case CXCursor_FieldDecl:
        case CXCursor_ParmDecl:
        case CXCursor_NonTypeTemplateParameter:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::VariableCompletionKind);
            break;
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
        case CXCursor_ClassDecl:
        case CXCursor_TemplateTypeParameter:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::ClassCompletionKind);
            break;
        case CXCursor_ClassTemplatePartialSpecialization:
        case CXCursor_ClassTemplate:
        case CXCursor_TemplateTemplateParameter:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::TemplateClassCompletionKind);
            break;
        case CXCursor_Namespace:
        case CXCursor_NamespaceAlias:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::NamespaceCompletionKind);
            break;
        case CXCursor_EnumDecl:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::EnumerationCompletionKind);
            break;
        case CXCursor_EnumConstantDecl:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::EnumeratorCompletionKind);
            break;
        case CXCursor_Constructor:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::ConstructorCompletionKind);
            break;
        case CXCursor_Destructor:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::DestructorCompletionKind);
            break;
        case CXCursor_MacroDefinition:
            extractMacroCompletionKind();
            break;
        case CXCursor_NotImplemented:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::KeywordCompletionKind);
            break;
        default:
            currentCodeCompletion_.setCompletionKind(CodeCompletion::Other);
    }
}

void CodeCompletionsExtracter::extractText() const
{
    const uint completionChunkCount = clang_getNumCompletionChunks(currentCxCodeCompleteResult.CompletionString);
    for (uint chunkIndex = 0; chunkIndex < completionChunkCount; ++chunkIndex) {
        const CXCompletionChunkKind chunkKind = clang_getCompletionChunkKind(currentCxCodeCompleteResult.CompletionString, chunkIndex);
        if (chunkKind == CXCompletionChunk_TypedText) {
            const ClangString text(clang_getCompletionChunkText(currentCxCodeCompleteResult.CompletionString, chunkIndex));
             currentCodeCompletion_.setText(text);
            break;
        }
    }
}

void CodeCompletionsExtracter::extractMethodCompletionKind() const
{
    CXCompletionString cxCompletionString = cxCodeCompleteResults->Results[cxCodeCompleteResultIndex].CompletionString;
    const unsigned annotationCount = clang_getCompletionNumAnnotations(cxCompletionString);

    for (uint annotationIndex = 0; annotationIndex < annotationCount; ++annotationIndex) {
        ClangString annotation = clang_getCompletionAnnotation(cxCompletionString, annotationIndex);

        if (annotation == Utf8StringLiteral("qt_signal")) {
            currentCodeCompletion_.setCompletionKind(CodeCompletion::SignalCompletionKind);
            return;
        }

        if (annotation == Utf8StringLiteral("qt_slot")) {
            currentCodeCompletion_.setCompletionKind(CodeCompletion::SlotCompletionKind);
            return;
        }
    }

    currentCodeCompletion_.setCompletionKind(CodeCompletion::FunctionCompletionKind);
}

void CodeCompletionsExtracter::extractMacroCompletionKind() const
{
    CXCompletionString cxCompletionString = cxCodeCompleteResults->Results[cxCodeCompleteResultIndex].CompletionString;

    const uint completionChunkCount = clang_getNumCompletionChunks(cxCompletionString);

    for (uint chunkIndex = 0; chunkIndex < completionChunkCount; ++chunkIndex) {
        CXCompletionChunkKind kind = clang_getCompletionChunkKind(cxCompletionString, chunkIndex);
        if (kind == CXCompletionChunk_Placeholder) {
            currentCodeCompletion_.setCompletionKind(CodeCompletion::FunctionCompletionKind);
            return;
        }
    }

    currentCodeCompletion_.setCompletionKind(CodeCompletion::PreProcessorCompletionKind);
}

void CodeCompletionsExtracter::extractPriority() const
{
    CXCompletionString cxCompletionString = cxCodeCompleteResults->Results[cxCodeCompleteResultIndex].CompletionString;
    quint32 priority = clang_getCompletionPriority(cxCompletionString);
    currentCodeCompletion_.setPriority(priority);
}

void CodeCompletionsExtracter::extractAvailability() const
{
    CXCompletionString cxCompletionString = cxCodeCompleteResults->Results[cxCodeCompleteResultIndex].CompletionString;
    CXAvailabilityKind cxAvailabilityKind = clang_getCompletionAvailability(cxCompletionString);

    switch (cxAvailabilityKind) {
        case CXAvailability_Available:
            currentCodeCompletion_.setAvailability(CodeCompletion::Available);
            break;
        case CXAvailability_Deprecated:
            currentCodeCompletion_.setAvailability(CodeCompletion::Deprecated);
            break;
        case CXAvailability_NotAvailable:
            currentCodeCompletion_.setAvailability(CodeCompletion::NotAvailable);
            break;
        case CXAvailability_NotAccessible:
            currentCodeCompletion_.setAvailability(CodeCompletion::NotAccessible);
            break;
    }
}

void CodeCompletionsExtracter::extractHasParameters() const
{
    const uint completionChunkCount = clang_getNumCompletionChunks(currentCxCodeCompleteResult.CompletionString);
    for (uint chunkIndex = 0; chunkIndex < completionChunkCount; ++chunkIndex) {
        const CXCompletionChunkKind chunkKind = clang_getCompletionChunkKind(currentCxCodeCompleteResult.CompletionString, chunkIndex);
        if (chunkKind == CXCompletionChunk_LeftParen) {
            const CXCompletionChunkKind nextChunkKind = clang_getCompletionChunkKind(currentCxCodeCompleteResult.CompletionString, chunkIndex + 1);
            currentCodeCompletion_.setHasParameters(nextChunkKind != CXCompletionChunk_RightParen);
            return;
        }
    }
}

bool CodeCompletionsExtracter::hasText(const Utf8String &text, CXCompletionString cxCompletionString) const
{
    const uint completionChunkCount = clang_getNumCompletionChunks(cxCompletionString);

    for (uint chunkIndex = 0; chunkIndex < completionChunkCount; ++chunkIndex) {
        const CXCompletionChunkKind chunkKind = clang_getCompletionChunkKind(cxCompletionString, chunkIndex);
        if (chunkKind == CXCompletionChunk_TypedText) {
            const ClangString currentText(clang_getCompletionChunkText(cxCompletionString, chunkIndex));
            return text == currentText;
        }
    }

    return false;
}

const CodeCompletion CodeCompletionsExtracter::currentCodeCompletion() const
{
    return currentCodeCompletion_;
}

#ifdef CODEMODELBACKEND_TESTS
void PrintTo(const CodeCompletionsExtracter &extracter, std::ostream *os)
{
    *os << "name: " << ::testing::PrintToString(extracter.currentCodeCompletion().text())
        << ", kind: " <<  ::testing::PrintToString(extracter.currentCodeCompletion().completionKind())
        << ", priority: " <<  extracter.currentCodeCompletion().priority()
        << ", kind: " <<  ::testing::PrintToString(extracter.currentCodeCompletion().availability());
}
#endif

} // namespace CodeModelBackEnd

