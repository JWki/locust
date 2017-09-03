#include <stdio.h>
#include <foundation/logging/logging.h>
#include <malloc.h>

#include <engine/runtime/entities/entities.h>

class SimpleFilterPolicy
{
public:
    bool Filter(fnd::logging::LogCriteria criteria)
    {
        return true;
    }
};
class SimpleFormatPolicy
{
public:
    void Format(char* buf, size_t bufSize, fnd::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "[%s]    ", criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};
class PrintfWriter
{
public:
    void Write(const char* msg)
    {
        printf("%s\n", msg);
    }
};

typedef fnd::logging::Logger<SimpleFilterPolicy, SimpleFormatPolicy, PrintfWriter> ConsoleLogger;

entity_system::Entity myEntity;
int nameIndex = 1;
const int numNames = 4; 
const char* names[] = { "Samuel", "Dean", "Robert", "Castiel" };

extern "C" __declspec(dllexport)
void Execute(entity_system::World* world, entity_system::EntitySystemInterface* entitySystem)
{
    using namespace fnd;
    ConsoleLogger consoleLogger;

    if (myEntity.id != entity_system::INVALID_ID) {
        if (nameIndex == 0) {
            entitySystem->DestroyEntity(world, myEntity);
            myEntity.id = 0;
        }
        else {
            entitySystem->SetEntityName(world, myEntity, names[nameIndex - 1]);
        }
        nameIndex = (nameIndex + 1) % (numNames + 1);
    }
    else {
        myEntity = entitySystem->CreateEntity(world);
        entitySystem->SetEntityName(world, myEntity, names[nameIndex - 1]);
        nameIndex = (nameIndex + 1) % (numNames + 1);
    }
  
    return;
}