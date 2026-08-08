// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/// @title Blockchain E-Voting System
/// @author Ishan
/// @notice Stores an election's candidates and votes immutably on-chain.
///         The backend/frontend never hold voter private keys — every vote
///         transaction here is signed directly by the voter's own wallet
///         (MetaMask), so the contract itself is the final source of truth
///         for "did this address already vote" and "how many votes does
///         each candidate have".
contract EVoting {

    /// @notice The account that deployed the contract (election commissioner).
    address public admin;

    /// @notice Human-readable title of this election, set at deploy time.
    string public electionTitle;

    /// @notice Whether voting is currently open. Only the admin can toggle
    /// this — candidates cannot be added once voting has started, and
    /// votes cannot be cast before it starts or after it ends.
    bool public votingOpen;

    /// @notice Unix timestamps defining the voting window.
    uint256 public startTime;
    uint256 public endTime;

    struct Candidate {
        uint256 id;
        string name;
        string partyName;
        uint256 voteCount;
    }

    Candidate[] public candidates;

    /// @notice Tracks which wallet addresses have already voted, so a
    /// double vote is rejected directly by the blockchain — this is the
    /// authoritative double-vote guard; the backend's MySQL unique
    /// constraint is a secondary, defense-in-depth check only.
    mapping(address => bool) public hasVoted;

    /// @notice Records which candidate each address voted for, so a voter
    /// (or an auditor) can independently verify their own vote on-chain.
    mapping(address => uint256) public voteChoice;

    event CandidateAdded(uint256 indexed candidateId, string name);
    event VoteCast(address indexed voter, uint256 indexed candidateId, uint256 timestamp);
    event VotingStatusChanged(bool isOpen);

    modifier onlyAdmin() {
        require(msg.sender == admin, "EVoting: caller is not the admin");
        _;
    }

    modifier votingIsOpen() {
        require(votingOpen, "EVoting: voting is not currently open");
        require(block.timestamp >= startTime, "EVoting: voting has not started yet");
        require(block.timestamp <= endTime, "EVoting: voting has ended");
        _;
    }

    /// @param _electionTitle Human-readable name for this election.
    /// @param _startTime Unix timestamp when voting opens.
    /// @param _endTime Unix timestamp when voting closes.
    constructor(string memory _electionTitle, uint256 _startTime, uint256 _endTime) {
        require(_endTime > _startTime, "EVoting: endTime must be after startTime");
        admin = msg.sender;
        electionTitle = _electionTitle;
        startTime = _startTime;
        endTime = _endTime;
        votingOpen = false;
    }

    /// @notice Adds a candidate to the ballot. Can only be called by the
    /// admin, and only before voting has opened (a candidate list must be
    /// finalized before the polls open, matching real election procedure).
    function addCandidate(string memory _name, string memory _partyName) external onlyAdmin {
        require(!votingOpen, "EVoting: cannot add candidates after voting has opened");

        uint256 newId = candidates.length;
        candidates.push(Candidate({
            id: newId,
            name: _name,
            partyName: _partyName,
            voteCount: 0
        }));

        emit CandidateAdded(newId, _name);
    }

    /// @notice Opens or closes voting. Only the admin can call this.
    function setVotingStatus(bool _isOpen) external onlyAdmin {
        votingOpen = _isOpen;
        emit VotingStatusChanged(_isOpen);
    }

    /// @notice Casts a vote for a candidate. Called directly by the voter's
    /// own wallet via MetaMask — msg.sender IS the voter, so there is no
    /// way for the backend or anyone else to vote on a user's behalf.
    function vote(uint256 _candidateId) external votingIsOpen {
        require(_candidateId < candidates.length, "EVoting: invalid candidate id");
        require(!hasVoted[msg.sender], "EVoting: this address has already voted");

        hasVoted[msg.sender] = true;
        voteChoice[msg.sender] = _candidateId;
        candidates[_candidateId].voteCount += 1;

        emit VoteCast(msg.sender, _candidateId, block.timestamp);
    }

    /// @notice Returns the total number of candidates on the ballot.
    function getCandidateCount() external view returns (uint256) {
        return candidates.length;
    }

    /// @notice Returns full details for a single candidate by id.
    function getCandidate(uint256 _candidateId)
        external
        view
        returns (uint256 id, string memory name, string memory partyName, uint256 voteCount)
    {
        require(_candidateId < candidates.length, "EVoting: invalid candidate id");
        Candidate memory c = candidates[_candidateId];
        return (c.id, c.name, c.partyName, c.voteCount);
    }

    /// @notice Returns every candidate at once, so the frontend can render
    /// the full ballot/results screen with a single call instead of
    /// looping getCandidate() once per candidate.
    function getAllCandidates() external view returns (Candidate[] memory) {
        return candidates;
    }

    /// @notice Convenience check used by the frontend to disable the
    /// "Vote" button if the connected wallet has already voted.
    function checkHasVoted(address _voter) external view returns (bool) {
        return hasVoted[_voter];
    }
}
