MATCH (x0)<-[:p6]-()<-[:p3]-()-[:p4]->(x1), (x1)<-[:p4]-()-[:p4]->(x2), (x0)<-[:p6]-()<-[:p3]-()-[:p0]->()-[:p2]->(x3), (x3)<-[:p2]-()-[:p3]->()-[:p6]->(x2) RETURN DISTINCT x3, x0, x1, x2;
