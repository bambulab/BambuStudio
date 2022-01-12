

function RefuseClause()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="user_clause";
	tSend['data']={};
	tSend['data']['action']="refuse";
	
	SendWXMessage( JSON.stringify(tSend) );
}