#include "logging.h"

#include <stdarg.h>

namespace lc
{
    namespace logging
    {
        namespace
        {
            static LoggerBase* g_loggerListHead = nullptr;
            static LoggerBase* g_loggerListTail = nullptr;
        }

        void LoggerBase::Register()
        {
            if (g_loggerListHead == nullptr) {
                g_loggerListHead = g_loggerListTail = this;
            }
            else {
                m_prev = g_loggerListTail;
                g_loggerListTail->m_next = this;
                g_loggerListTail = this;
            }
        }

        void LoggerBase::Unregister()
        {
            if (m_next) { m_next->m_prev = m_prev; }
            if (m_prev) { m_prev->m_next = m_next; }
            if (g_loggerListHead == this) { g_loggerListHead = g_loggerListTail = nullptr; }
            if (g_loggerListTail == this) { g_loggerListTail = m_prev; }
        }

        LoggerBase::LoggerBase()
        {
            Register();
        }

        LoggerBase::~LoggerBase()
        {
            Unregister();
        }


        void Log(LogChannel channel, LogLevel level, LogVerbosity verbosity, SourceInfo scInfo, const char* format, ...)
        {
            
            LoggerBase* it = g_loggerListHead;
            while (it != nullptr) {
                va_list args;
                va_start(args, format);
                it->Log(channel, level, verbosity, scInfo, format, args);
                va_end(args);
                it = it->GetNext();
            }
        }
    }
}