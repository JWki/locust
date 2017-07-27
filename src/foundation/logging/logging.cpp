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

  

        LoggerBase::LoggerBase()
        {
            if (g_loggerListHead == nullptr) {
                g_loggerListHead = g_loggerListTail = this;
            }
            else {
                g_loggerListTail->m_next = this;
                this->m_prev = g_loggerListTail;
                g_loggerListTail = this;
            }
        }

        LoggerBase::~LoggerBase()
        {
            if (g_loggerListHead == this) { g_loggerListHead = g_loggerListTail = nullptr; }
            if (g_loggerListTail == this) { g_loggerListTail = m_prev; }
            auto next = m_next;
            auto previous = m_prev;

            if (previous) { previous->m_next = next; }
            if (next) { next->m_prev = previous; }

            m_next = m_prev = nullptr;
        }


        void LoggerBase::LogDispatch(LogChannel channel, LogLevel level, LogVerbosity verbosity, SourceInfo scInfo, const char* format, ...)
        {
            LoggerBase* it = g_loggerListHead;
            while (it != nullptr) {
                va_list args;
                va_start(args, format);
                it->Log(channel, level, verbosity, scInfo, format, args);
                va_end(args);
                it = it->m_next;
            }
        }
    }
}