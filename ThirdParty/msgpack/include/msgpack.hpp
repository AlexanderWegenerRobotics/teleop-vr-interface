#pragma once

#include "msgpack/object.hpp"
#include "msgpack/iterator.hpp"
#include "msgpack/zone.hpp"
#include "msgpack/pack.hpp"
#include "msgpack/null_visitor.hpp"
#include "msgpack/parse.hpp"
#include "msgpack/unpack.hpp"
#include "msgpack/x3_parse.hpp"
#include "msgpack/x3_unpack.hpp"
#include "msgpack/sbuffer.hpp"
#include "msgpack/vrefbuffer.hpp"
#include "msgpack/version.hpp"
#include "msgpack/type.hpp"


#ifndef MSGPACK_NO_BOOST
#define MSGPACK_NO_BOOST
#endif