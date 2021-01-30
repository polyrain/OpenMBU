//-----------------------------------------------------------------------------
// Torque Game Engine
// Copyright (c) 2002 GarageGames.Com
//-----------------------------------------------------------------------------

#include "platform/platform.h"
#include "core/dnet.h"
#include "console/simBase.h"
#include "sim/netStringTable.h"
#include "core/stringTable.h"

NetStringTable* gNetStringTable = NULL;

NetStringTable::NetStringTable()
{
    firstFree = 1;
    firstValid = 1;

    table = (Entry*)dMalloc(sizeof(Entry) * InitialSize);
    size = InitialSize;
    for (U32 i = 0; i < InitialSize; i++)
    {
        table[i].next = i + 1;
        table[i].refCount = 0;
        table[i].scriptRefCount = 0;
    }
    table[InitialSize - 1].next = InvalidEntry;
    for (U32 j = 0; j < HashTableSize; j++)
        hashTable[j] = 0;
    allocator = new DataChunker(DataChunkerSize);
}

NetStringTable::~NetStringTable()
{
    delete allocator;
}

void NetStringTable::incStringRef(U32 id)
{
    AssertFatal(table[id].refCount != 0 || table[id].scriptRefCount != 0, "Cannot inc ref count from zero.");
    table[id].refCount++;
}

void NetStringTable::incStringRefScript(U32 id)
{
    AssertFatal(table[id].refCount != 0 || table[id].scriptRefCount != 0, "Cannot inc ref count from zero.");
    table[id].scriptRefCount++;
}

U32 NetStringTable::addString(const char* string)
{
    U32 hash = _StringTable::hashString(string);
    U32 bucket = hash % HashTableSize;
    for (U32 walk = hashTable[bucket]; walk; walk = table[walk].next)
    {
        if (!dStrcmp(table[walk].string, string))
        {
            table[walk].refCount++;
            return walk;
        }
    }
    U32 e = firstFree;
    firstFree = table[e].next;
    if (firstFree == InvalidEntry)
    {
        // in this case, we should expand the table for next time...
        U32 newSize = size * 2;
        table = (Entry*)dRealloc(table, newSize * sizeof(Entry));
        for (U32 i = size; i < newSize; i++)
        {
            table[i].next = i + 1;
            table[i].refCount = 0;
            table[i].scriptRefCount = 0;
        }
        firstFree = size;
        table[newSize - 1].next = InvalidEntry;
        size = newSize;
    }
    table[e].refCount++;
    table[e].string = (char*)allocator->alloc(dStrlen(string) + 1);
    dStrcpy(table[e].string, string);
    table[e].next = hashTable[bucket];
    hashTable[bucket] = e;
    table[e].link = firstValid;
    table[firstValid].prevLink = e;
    firstValid = e;
    table[e].prevLink = 0;
    return e;
}

U32 GameAddTaggedString(const char* string)
{
    return gNetStringTable->addString(string);
}

const char* NetStringTable::lookupString(U32 id)
{
    if (table[id].refCount == 0 && table[id].scriptRefCount == 0)
        return NULL;
    return table[id].string;
}

void NetStringTable::removeString(U32 id, bool script)
{
    if (!script)
    {
        AssertFatal(table[id].refCount != 0, "Error, ref count is already 0!!");
        if (--table[id].refCount)
            return;
        if (table[id].scriptRefCount)
            return;
    }
    else
    {
        // If both ref counts are already 0, this id is not valid. Ignore
        // the remove
        if (table[id].scriptRefCount == 0 && table[id].refCount == 0)
            return;

        if (table[id].scriptRefCount == 0 && table[id].refCount)
        {
            Con::errorf("removeTaggedString failed!  Ref count is already 0 for string: %s", table[id].string);
            return;
        }
        if (--table[id].scriptRefCount)
            return;
        if (table[id].refCount)
            return;
    }
    // unlink first:
    U32 prev = table[id].prevLink;
    U32 next = table[id].link;
    if (next)
        table[next].prevLink = prev;
    if (prev)
        table[prev].link = next;
    else
        firstValid = next;
    // remove it from the hash table
    U32 hash = _StringTable::hashString(table[id].string);
    U32 bucket = hash % HashTableSize;
    for (U32* walk = &hashTable[bucket]; *walk; walk = &table[*walk].next)
    {
        if (*walk == id)
        {
            *walk = table[id].next;
            break;
        }
    }
    table[id].next = firstFree;
    firstFree = id;
}

void NetStringTable::repack()
{
    DataChunker* newAllocator = new DataChunker(DataChunkerSize);
    for (U32 walk = firstValid; walk; walk = table[walk].link)
    {
        const char* prevStr = table[walk].string;


        table[walk].string = (char*)newAllocator->alloc(dStrlen(prevStr) + 1);
        dStrcpy(table[walk].string, prevStr);
    }
    delete allocator;
    allocator = newAllocator;
}

void NetStringTable::create()
{
    AssertFatal(gNetStringTable == NULL, "Error, calling NetStringTable::create twice.");
    gNetStringTable = new NetStringTable();
}

void NetStringTable::destroy()
{
    AssertFatal(gNetStringTable != NULL, "Error, not calling NetStringTable::create.");
    delete gNetStringTable;
    gNetStringTable = NULL;
}

void NetStringTable::expandString(StringHandle& inString, char* buf, U32 bufSize, U32 argc, const char** argv)
{
    buf[0] = StringTagPrefixByte;
    dSprintf(buf + 1, bufSize - 1, "%d ", inString.getIndex());

    const char* string = inString.getString();
    if (string != NULL) {
        U32 index = dStrlen(buf);
        while (index < bufSize)
        {
            char c = *string++;
            if (c == '%')
            {
                c = *string++;
                if (c >= '1' && c <= '9')
                {
                    U32 strIndex = c - '1';
                    if (strIndex >= argc)
                        continue;
                    // start copying out of arg index
                    const char* copy = argv[strIndex];
                    // skip past any tags:
                    if (*copy == StringTagPrefixByte)
                    {
                        while (*copy && *copy != ' ')
                            copy++;
                        if (*copy)
                            copy++;
                    }

                    while (*copy && index < bufSize)
                        buf[index++] = *copy++;
                    continue;
                }
            }
            buf[index++] = c;
            if (!c)
                break;
        }
        buf[bufSize - 1] = 0;
    }
    else {
        dStrcat(buf, "<NULL>");
    }
}

#if defined(TORQUE_DEBUG)
void NetStringTable::dumpToConsole()
{
    U32 count = 0;
    S32 maxIndex = -1;
    for (U32 i = 0; i < size; i++)
    {
        if (table[i].refCount > 0 || table[i].scriptRefCount > 0)
        {
            Con::printf("%d: \"%c%s%c\" REF: %d", i, 0x10, table[i].string, 0x11, table[i].refCount);
            if (maxIndex == -1 || table[i].refCount > table[maxIndex].refCount)
                maxIndex = i;
            count++;
        }
    }
    Con::printf(">> STRINGS: %d MAX REF COUNT: %d \"%c%s%c\" <<",
        count,
        (maxIndex == -1) ? 0 : table[maxIndex].refCount,
        0x10,
        (maxIndex == -1) ? "" : table[maxIndex].string,
        0x11);
}

ConsoleFunctionGroupBegin(NetStringTable, "Debug functions for the NetStringTable.");

ConsoleFunction(dumpNetStringTable, void, 1, 1, "dumpNetStringTable()")
{
    argc; argv;
    gNetStringTable->dumpToConsole();
}

ConsoleFunctionGroupEnd(NetStringTable);

#endif // DEBUG
