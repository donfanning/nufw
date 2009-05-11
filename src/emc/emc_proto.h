/*
 ** Copyright(C) 2009 INL
 ** Written by Pierre Chifflier <chifflier@inl.fr>
 **     INL : http://www.inl.fr/
 **
 ** $Id$
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, version 3 of the License.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program; if not, write to the Free Software
 ** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __EMC_PROTO_H__
#define __EMC_PROTO_H__

enum emc_proto_version_t {
	PROTO_VERSION_EMC_V1 = 1,
};

enum emc_command_t {
	EMC_NOP = 0,
	EMC_HELLO,
	EMC_CLIENT_CONNECTION_REQUEST,
};

#endif /* __EMC_PROTO_H__ */
