// ============================================================================
//  test/EVoting.test.js
//  Hardhat/Chai test suite for the EVoting smart contract.
//  Run with: npx hardhat test
// ============================================================================

const { expect } = require("chai");
const { ethers } = require("hardhat");
const { anyValue } = require("@nomicfoundation/hardhat-chai-matchers/withArgs");

describe("EVoting", function () {
  let EVoting, evoting, admin, voter1, voter2, voter3;
  let startTime, endTime;

  beforeEach(async function () {
    [admin, voter1, voter2, voter3] = await ethers.getSigners();

    const now = Math.floor(Date.now() / 1000);
    startTime = now - 10; // already started, for easy testing
    endTime = now + 3600; // ends in 1 hour

    EVoting = await ethers.getContractFactory("EVoting");
    evoting = await EVoting.deploy("Test Election", startTime, endTime);
    await evoting.waitForDeployment();
  });

  describe("Deployment", function () {
    it("sets the deployer as admin", async function () {
      expect(await evoting.admin()).to.equal(admin.address);
    });

    it("sets the election title", async function () {
      expect(await evoting.electionTitle()).to.equal("Test Election");
    });

    it("starts with voting closed", async function () {
      expect(await evoting.votingOpen()).to.equal(false);
    });
  });

  describe("Candidate management", function () {
    it("allows admin to add a candidate", async function () {
      await expect(evoting.addCandidate("Alice", "Party A"))
        .to.emit(evoting, "CandidateAdded")
        .withArgs(0, "Alice");

      expect(await evoting.getCandidateCount()).to.equal(1);
    });

    it("rejects candidate addition from a non-admin account", async function () {
      await expect(
        evoting.connect(voter1).addCandidate("Bob", "Party B")
      ).to.be.revertedWith("EVoting: caller is not the admin");
    });

    it("rejects adding candidates after voting has opened", async function () {
      await evoting.addCandidate("Alice", "Party A");
      await evoting.setVotingStatus(true);

      await expect(
        evoting.addCandidate("Late Candidate", "Party C")
      ).to.be.revertedWith("EVoting: cannot add candidates after voting has opened");
    });
  });

  describe("Voting", function () {
    beforeEach(async function () {
      await evoting.addCandidate("Alice", "Party A");
      await evoting.addCandidate("Bob", "Party B");
      await evoting.setVotingStatus(true);
    });

    it("allows a voter to cast a vote", async function () {
      await expect(evoting.connect(voter1).vote(0))
        .to.emit(evoting, "VoteCast")
        .withArgs(voter1.address, 0, anyValue);

      const candidate = await evoting.getCandidate(0);
      expect(candidate.voteCount).to.equal(1);
    });

    it("prevents double voting from the same address", async function () {
      await evoting.connect(voter1).vote(0);

      await expect(evoting.connect(voter1).vote(1)).to.be.revertedWith(
        "EVoting: this address has already voted"
      );
    });

    it("rejects a vote for a non-existent candidate id", async function () {
      await expect(evoting.connect(voter1).vote(99)).to.be.revertedWith(
        "EVoting: invalid candidate id"
      );
    });

    it("rejects voting when voting is closed", async function () {
      await evoting.setVotingStatus(false);
      await expect(evoting.connect(voter1).vote(0)).to.be.revertedWith(
        "EVoting: voting is not currently open"
      );
    });

    it("correctly tallies votes across multiple voters", async function () {
      await evoting.connect(voter1).vote(0);
      await evoting.connect(voter2).vote(0);
      await evoting.connect(voter3).vote(1);

      const alice = await evoting.getCandidate(0);
      const bob = await evoting.getCandidate(1);

      expect(alice.voteCount).to.equal(2);
      expect(bob.voteCount).to.equal(1);
    });

    it("correctly reports checkHasVoted", async function () {
      expect(await evoting.checkHasVoted(voter1.address)).to.equal(false);
      await evoting.connect(voter1).vote(0);
      expect(await evoting.checkHasVoted(voter1.address)).to.equal(true);
    });
  });

  describe("Access control", function () {
    it("only allows admin to toggle voting status", async function () {
      await expect(
        evoting.connect(voter1).setVotingStatus(true)
      ).to.be.revertedWith("EVoting: caller is not the admin");
    });
  });
});
