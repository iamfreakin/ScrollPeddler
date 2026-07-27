#include "Core/SPInteractionTypes.h"

bool SPAreSameInteractionRequest(
	const FSPInteractionRequest& A,
	const FSPInteractionRequest& B)
{
	return A.RequestId == B.RequestId
		&& A.Action == B.Action
		&& A.TargetInstanceId == B.TargetInstanceId
		&& A.ExpectedRevision == B.ExpectedRevision;
}

FSPInteractionReplayLedger::FSPInteractionReplayLedger(const int32 InCapacity)
	: Capacity(FMath::Max(1, InCapacity))
{
}

FSPInteractionReplayLedger::ELookup FSPInteractionReplayLedger::Find(
	const FSPInteractionRequest& Request,
	FSPInteractionResult& OutResult) const
{
	const FRecord* Existing = Records.Find(Request.RequestId);
	if (!Existing)
	{
		return ELookup::NotFound;
	}

	if (!SPAreSameInteractionRequest(Existing->Request, Request))
	{
		return ELookup::Conflict;
	}

	OutResult = Existing->Result;
	return ELookup::ExactReplay;
}

bool FSPInteractionReplayLedger::Record(
	const FSPInteractionRequest& Request,
	const FSPInteractionResult& Result)
{
	if (!Request.IsValid()
		|| Result.RequestId != Request.RequestId
		|| Result.Action != Request.Action)
	{
		return false;
	}

	if (const FRecord* Existing = Records.Find(Request.RequestId))
	{
		return SPAreSameInteractionRequest(Existing->Request, Request)
			&& Existing->Result.Code == Result.Code
			&& Existing->Result.AuthoritativeRevision == Result.AuthoritativeRevision
			&& Existing->Result.FeedbackTag == Result.FeedbackTag;
	}

	FRecord& NewRecord = Records.Add(Request.RequestId);
	NewRecord.Request = Request;
	NewRecord.Result = Result;
	InsertionOrder.Add(Request.RequestId);
	TrimToCapacity();
	return true;
}

void FSPInteractionReplayLedger::Reset()
{
	Records.Reset();
	InsertionOrder.Reset();
}

void FSPInteractionReplayLedger::TrimToCapacity()
{
	while (InsertionOrder.Num() > Capacity)
	{
		const int64 OldestRequestId = InsertionOrder[0];
		InsertionOrder.RemoveAt(0, 1, EAllowShrinking::No);
		Records.Remove(OldestRequestId);
	}
}
