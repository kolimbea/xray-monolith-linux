////////////////////////////////////////////////////////////////////////////
//	Module 		: CEdge.h
//	Created 	: 14.01.2004
//  Modified 	: 19.02.2005
//	Author		: Dmitriy Iassenev
//	Description : Graph edge class template
////////////////////////////////////////////////////////////////////////////

#pragma once

template <typename _edge_weight_type_, typename _vertex_type_>
class CEdgeBase
{
public:
	typedef _edge_weight_type_ _edge_weight_type; // parameter renamed: ISO C++ forbids redeclaring it
	typedef _vertex_type_ _vertex_type; // parameter renamed: ISO C++ forbids redeclaring it
	typedef typename _vertex_type_::_vertex_id_type _vertex_id_type;

private:
	_edge_weight_type_ m_weight;
	_vertex_type_* m_vertex;

public:
	IC CEdgeBase(const _edge_weight_type_& weight, _vertex_type_* vertex);
	IC const _edge_weight_type_& weight() const;
	IC _vertex_type_* vertex() const;
	IC const _vertex_id_type& vertex_id() const;
};

template <typename _edge_weight_type, typename _vertex_type, typename _edge_data_type>
class CEdge : 
	public CEdgeBase<_edge_weight_type,_vertex_type> 
{
private:
	typedef CEdgeBase<_edge_weight_type, _vertex_type> inherited;

public:
	typedef typename inherited::_vertex_id_type _vertex_id_type; // from the dependent base

private:
	_edge_data_type m_data;

public:
	IC CEdge(const _edge_weight_type& weight, _vertex_type* vertex);
	IC bool operator==(const _vertex_id_type& vertex_id) const;
	IC bool operator==(const CEdge& obj) const;
	IC const _edge_data_type& data() const;
	IC _edge_data_type& data();
};

template <typename _edge_weight_type, typename _vertex_type>
class CEdge<_edge_weight_type, _vertex_type, xr_empty> : 
	public CEdgeBase<_edge_weight_type,_vertex_type> {
private:
	typedef CEdgeBase<_edge_weight_type, _vertex_type> inherited;
	using _vertex_id_type = inherited::_vertex_id_type;

public:
	IC CEdge(const _edge_weight_type& weight, _vertex_type* vertex);
	IC bool operator==(const _vertex_id_type& vertex_id) const;
	IC bool operator==(const CEdge& obj) const;
};

#include "graph_edge_inline.h"
