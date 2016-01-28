/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "clangassistproposalitem.h"

#include "clangcompletionchunkstotextconverter.h"

#include <cplusplus/Icons.h>
#include <cplusplus/MatchingText.h>
#include <cplusplus/Token.h>

#include <texteditor/completionsettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorsettings.h>

using namespace CPlusPlus;
using namespace ClangBackEnd;

namespace ClangCodeModel {
namespace Internal {

bool ClangAssistProposalItem::prematurelyApplies(const QChar &typedChar) const
{
    bool applies = false;

    if (m_completionOperator == T_SIGNAL || m_completionOperator == T_SLOT)
        applies = QString::fromLatin1("(,").contains(typedChar);
    else if (m_completionOperator == T_STRING_LITERAL || m_completionOperator == T_ANGLE_STRING_LITERAL)
        applies = (typedChar == QLatin1Char('/')) && text().endsWith(QLatin1Char('/'));
    else if (codeCompletion().completionKind() == CodeCompletion::ObjCMessageCompletionKind)
        applies = QString::fromLatin1(";.,").contains(typedChar);
    else
        applies = QString::fromLatin1(";.,:(").contains(typedChar);

    if (applies)
        m_typedChar = typedChar;

    return applies;
}

bool ClangAssistProposalItem::implicitlyApplies() const
{
    return false;
}

static bool hasOnlyBlanksBeforeCursorInLine(QTextCursor textCursor)
{
    textCursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);

    const auto textBeforeCursor = textCursor.selectedText();

    const auto nonSpace = std::find_if(textBeforeCursor.cbegin(),
                                       textBeforeCursor.cend(),
                                       [] (const QChar &signBeforeCursor) {
        return !signBeforeCursor.isSpace();
    });

    return nonSpace == textBeforeCursor.cend();
}

void ClangAssistProposalItem::apply(TextEditor::TextEditorWidget *editorWidget,
                                    int basePosition) const
{
    const CodeCompletion ccr = codeCompletion();

    QString textToBeInserted = text();
    QString extraChars;
    int extraLength = 0;
    int cursorOffset = 0;

    bool autoParenthesesEnabled = true;
    if (m_completionOperator == T_SIGNAL || m_completionOperator == T_SLOT) {
        extraChars += QLatin1Char(')');
        if (m_typedChar == QLatin1Char('(')) // Eat the opening parenthesis
            m_typedChar = QChar();
    } else if (m_completionOperator == T_STRING_LITERAL || m_completionOperator == T_ANGLE_STRING_LITERAL) {
        if (!textToBeInserted.endsWith(QLatin1Char('/'))) {
            extraChars += QLatin1Char((m_completionOperator == T_ANGLE_STRING_LITERAL) ? '>' : '"');
        } else {
            if (m_typedChar == QLatin1Char('/')) // Eat the slash
                m_typedChar = QChar();
        }
    } else if (ccr.completionKind() == CodeCompletion::KeywordCompletionKind) {
        CompletionChunksToTextConverter converter;
        converter.setupForKeywords();

        converter.parseChunks(ccr.chunks());

        textToBeInserted = converter.text();
        if (converter.hasPlaceholderPositions())
            cursorOffset = converter.placeholderPositions().at(0) - converter.text().size();
    } else if (ccr.completionKind() == CodeCompletion::NamespaceCompletionKind) {
        CompletionChunksToTextConverter converter;

        converter.parseChunks(ccr.chunks()); // Appends "::" after name space name

        textToBeInserted = converter.text();
    } else if (!ccr.text().isEmpty()) {
        const TextEditor::CompletionSettings &completionSettings =
                TextEditor::TextEditorSettings::instance()->completionSettings();
        const bool autoInsertBrackets = completionSettings.m_autoInsertBrackets;

        if (autoInsertBrackets &&
                (ccr.completionKind() == CodeCompletion::FunctionCompletionKind
                 || ccr.completionKind() == CodeCompletion::DestructorCompletionKind
                 || ccr.completionKind() == CodeCompletion::SignalCompletionKind
                 || ccr.completionKind() == CodeCompletion::SlotCompletionKind)) {
            // When the user typed the opening parenthesis, he'll likely also type the closing one,
            // in which case it would be annoying if we put the cursor after the already automatically
            // inserted closing parenthesis.
            const bool skipClosingParenthesis = m_typedChar != QLatin1Char('(');

            if (completionSettings.m_spaceAfterFunctionName)
                extraChars += QLatin1Char(' ');
            extraChars += QLatin1Char('(');
            if (m_typedChar == QLatin1Char('('))
                m_typedChar = QChar();

            // If the function doesn't return anything, automatically place the semicolon,
            // unless we're doing a scope completion (then it might be function definition).
            const QChar characterAtCursor = editorWidget->characterAt(editorWidget->position());
            bool endWithSemicolon = m_typedChar == QLatin1Char(';')/*
                                            || (function->returnType()->isVoidType() && m_completionOperator != T_COLON_COLON)*/; //###
            const QChar semicolon = m_typedChar.isNull() ? QLatin1Char(';') : m_typedChar;

            if (endWithSemicolon && characterAtCursor == semicolon) {
                endWithSemicolon = false;
                m_typedChar = QChar();
            }

            // If the function takes no arguments, automatically place the closing parenthesis
            if (!isOverloaded() && !ccr.hasParameters() && skipClosingParenthesis) {
                extraChars += QLatin1Char(')');
                if (endWithSemicolon) {
                    extraChars += semicolon;
                    m_typedChar = QChar();
                }
            } else if (autoParenthesesEnabled) {
                const QChar lookAhead = editorWidget->characterAt(editorWidget->position() + 1);
                if (MatchingText::shouldInsertMatchingText(lookAhead)) {
                    extraChars += QLatin1Char(')');
                    --cursorOffset;
                    if (endWithSemicolon) {
                        extraChars += semicolon;
                        --cursorOffset;
                        m_typedChar = QChar();
                    }
                }
            }
        }

#if 0
        if (autoInsertBrackets && data().canConvert<CompleteFunctionDeclaration>()) {
            if (m_typedChar == QLatin1Char('('))
                m_typedChar = QChar();

            // everything from the closing parenthesis on are extra chars, to
            // make sure an auto-inserted ")" gets replaced by ") const" if necessary
            int closingParen = toInsert.lastIndexOf(QLatin1Char(')'));
            extraChars = toInsert.mid(closingParen);
            toInsert.truncate(closingParen);
        }
#endif
    }

    // Append an unhandled typed character, adjusting cursor offset when it had been adjusted before
    if (!m_typedChar.isNull()) {
        extraChars += m_typedChar;
        if (cursorOffset != 0)
            --cursorOffset;
    }

    // Avoid inserting characters that are already there
    const int endsPosition = editorWidget->position(TextEditor::EndOfLinePosition);
    const QString existingText = editorWidget->textAt(editorWidget->position(), endsPosition - editorWidget->position());
    int existLength = 0;
    if (!existingText.isEmpty() && ccr.completionKind() != CodeCompletion::KeywordCompletionKind) {
        // Calculate the exist length in front of the extra chars
        existLength = textToBeInserted.length() - (editorWidget->position() - basePosition);
        while (!existingText.startsWith(textToBeInserted.right(existLength))) {
            if (--existLength == 0)
                break;
        }
    }
    for (int i = 0; i < extraChars.length(); ++i) {
        const QChar a = extraChars.at(i);
        const QChar b = editorWidget->characterAt(editorWidget->position() + i + existLength);
        if (a == b)
            ++extraLength;
        else
            break;
    }

    textToBeInserted += extraChars;

    // Insert the remainder of the name
    const int length = editorWidget->position() - basePosition + existLength + extraLength;
    const auto textToBeReplaced = editorWidget->textAt(basePosition, length);

    if (textToBeReplaced != textToBeInserted) {
        editorWidget->setCursorPosition(basePosition);
        editorWidget->replace(length, textToBeInserted);
        if (cursorOffset)
            editorWidget->setCursorPosition(editorWidget->position() + cursorOffset);

        // indent the statement
        if (ccr.completionKind() == CodeCompletion::KeywordCompletionKind) {
            auto selectionCursor = editorWidget->textCursor();
            selectionCursor.setPosition(basePosition);
            selectionCursor.setPosition(basePosition + textToBeInserted.size(), QTextCursor::KeepAnchor);

            auto basePositionCursor = editorWidget->textCursor();
            basePositionCursor.setPosition(basePosition);
            if (hasOnlyBlanksBeforeCursorInLine(basePositionCursor))
                editorWidget->textDocument()->autoIndent(selectionCursor);
        }
    }
}

void ClangAssistProposalItem::setText(const QString &text)
{
    m_text = text;
}

QString ClangAssistProposalItem::text() const
{
    return m_text;
}

QIcon ClangAssistProposalItem::icon() const
{
    using CPlusPlus::Icons;
    static const CPlusPlus::Icons m_icons;
    static const char SNIPPET_ICON_PATH[] = ":/texteditor/images/snippet.png";
    static const QIcon snippetIcon = QIcon(QLatin1String(SNIPPET_ICON_PATH));

    switch (m_codeCompletion.completionKind()) {
        case CodeCompletion::ClassCompletionKind:
        case CodeCompletion::TemplateClassCompletionKind:
        case CodeCompletion::TypeAliasCompletionKind:
            return m_icons.iconForType(Icons::ClassIconType);
        case CodeCompletion::EnumerationCompletionKind:
            return m_icons.iconForType(Icons::EnumIconType);
        case CodeCompletion::EnumeratorCompletionKind:
            return m_icons.iconForType(Icons::EnumeratorIconType);
        case CodeCompletion::ConstructorCompletionKind:
        case CodeCompletion::DestructorCompletionKind:
        case CodeCompletion::FunctionCompletionKind:
        case CodeCompletion::TemplateFunctionCompletionKind:
        case CodeCompletion::ObjCMessageCompletionKind:
            switch (m_codeCompletion.availability()) {
                case CodeCompletion::Available:
                case CodeCompletion::Deprecated:
                    return m_icons.iconForType(Icons::FuncPublicIconType);
                default:
                    return m_icons.iconForType(Icons::FuncPrivateIconType);
            }
        case CodeCompletion::SignalCompletionKind:
            return m_icons.iconForType(Icons::SignalIconType);
        case CodeCompletion::SlotCompletionKind:
            switch (m_codeCompletion.availability()) {
                case CodeCompletion::Available:
                case CodeCompletion::Deprecated:
                    return m_icons.iconForType(Icons::SlotPublicIconType);
                case CodeCompletion::NotAccessible:
                case CodeCompletion::NotAvailable:
                    return m_icons.iconForType(Icons::SlotPrivateIconType);
            }
        case CodeCompletion::NamespaceCompletionKind:
            return m_icons.iconForType(Icons::NamespaceIconType);
        case CodeCompletion::PreProcessorCompletionKind:
            return m_icons.iconForType(Icons::MacroIconType);
        case CodeCompletion::VariableCompletionKind:
            switch (m_codeCompletion.availability()) {
                case CodeCompletion::Available:
                case CodeCompletion::Deprecated:
                    return m_icons.iconForType(Icons::VarPublicIconType);
                default:
                    return m_icons.iconForType(Icons::VarPrivateIconType);
            }
        case CodeCompletion::KeywordCompletionKind:
            return m_icons.iconForType(Icons::KeywordIconType);
        case CodeCompletion::ClangSnippetKind:
            return snippetIcon;
        case CodeCompletion::Other:
            return m_icons.iconForType(Icons::UnknownIconType);
    }

    return QIcon();
}

QString ClangAssistProposalItem::detail() const
{
    QString detail = CompletionChunksToTextConverter::convertToToolTipWithHtml(m_codeCompletion.chunks());

    if (!m_codeCompletion.briefComment().isEmpty())
        detail += QStringLiteral("\n\n") + m_codeCompletion.briefComment().toString();

    return detail;
}

bool ClangAssistProposalItem::isSnippet() const
{
    return false;
}

bool ClangAssistProposalItem::isValid() const
{
    return true;
}

quint64 ClangAssistProposalItem::hash() const
{
    return 0;
}

void ClangAssistProposalItem::keepCompletionOperator(unsigned compOp)
{
    m_completionOperator = compOp;
}

bool ClangAssistProposalItem::isOverloaded() const
{
    return !m_overloads.isEmpty();
}

void ClangAssistProposalItem::addOverload(const CodeCompletion &ccr)
{
    m_overloads.append(ccr);
}

void ClangAssistProposalItem::setCodeCompletion(const CodeCompletion &codeCompletion)
{
    m_codeCompletion = codeCompletion;
}

const ClangBackEnd::CodeCompletion &ClangAssistProposalItem::codeCompletion() const
{
    return m_codeCompletion;
}

} // namespace Internal
} // namespace ClangCodeModel

